/****************************************************************************
 *
 *      Description: Example startup file for the SOS project.
 *
 *      Author:		    Godfrey van der Linden(gvdl)
 *      Original Author:    Ben Leslie
 *      Modified by:        Vivek Shah
 *
 ****************************************************************************/


#include <assert.h>
#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <clock.h>
#include <nslu2.h>

#include "l4.h"
#include "libsos.h"
#include "network.h"

#include "pager.h"
#include "frames.h"
#include "sos_rpc.h"

#include "../helpers/ipc_defs.h"

// verbose is now in rpc.h

#define ONE_MEG	    (1 * 1024 * 1024)

#define HEAP_SIZE   ONE_MEG /* 1 MB heap */

/* Set aside some memory for a stack for the
 * first user task 
 */
static L4_Word_t init_stack_address = 0x9000000;
static L4_Word_t user_stack_address = 0x8000000;
static L4_Word_t rpc_stack_address  = 0x7500000;
static L4_BootRec_t* binfo_rec = 0;
static L4_ThreadId_t rpc_threadId;

// Init thread - This function starts up our device drivers and runs the first
// user program.
static void
init_thread(void)
{
    // Initialise the network for libsos_logf_init
    network_init();
    //Initialise swap which creates the swap file if it is not present and needs to be done
    //after network init
    initialise_swap();
    // Loop through the BootInfo starting executables
    int i;
    L4_Word_t task = 0;
    for (i = 1; (binfo_rec = sos_get_binfo_rec(i)); i++) {
	if (L4_BootRec_Type(binfo_rec) != L4_BootInfo_SimpleExec || 
	    strcmp(L4_SimpleExec_Cmdline(binfo_rec),"sosh") != 0)
	    continue;
	// Must be a SimpleExec boot info record
	dprintf(0, "Found exec: %d %s\n", i, L4_SimpleExec_Cmdline(binfo_rec));
	// Start a new task with this program
	L4_ThreadId_t newtid = sos_task_new(++task, L4_Pager(), 
		(void *) L4_SimpleExec_TextVstart(binfo_rec),
		(void *) user_stack_address);
	dprintf(0,"Task id %lx\n",newtid.raw);
	dprintf(0, "Created task: %lx\n", sos_tid2task(newtid));
	dprintf(0, "Root thread ID: %lx\n", L4_Myself());
    }
    // Thread finished - block forever
    for (;;)
	sos_usleep(30 * 1000 * 1000);
}

/*
  Syscall loop.

  This implements a very simple, single threaded functions for 
  recieving any IPCs and dispatching to the correct subsystem.

  It currently handles pagefaults, interrupts and special sigma0
  requests.
*/

static __inline__ void 
syscall_loop(void)
{
    dprintf(3, "Entering %s\n", __FUNCTION__);

    // Setup which messages we will recieve
    L4_Accept(L4_UntypedWordsAcceptor);
    
    int send = 0;
    L4_Msg_t msg;
    L4_ThreadId_t tid = L4_nilthread;

    for (;;) {
	L4_MsgTag_t tag;

	// Wait for a message, sometimes sending a reply
	if (!send)
	    tag = L4_Wait(&tid); // Nothing to send, so we just wait
	else
	    tag = L4_ReplyWait(tid, &tid); // Reply to caller and wait for IPC
	
	if (L4_IpcFailed(tag)) {
	    L4_Word_t ec = L4_ErrorCode();
	    dprintf(0, "%s: IPC error\n", __FUNCTION__);
	    sos_print_error(ec);
	    assert( !(ec & 1) );	// Check for recieve error and bail
	    send = 0;
	    continue;
	}

	// At this point we have, probably, recieved an IPC
	L4_MsgStore(tag, &msg); /* Get the tag */

	dprintf(2, "%s: got msg from %lx, (%d %p)\n", __FUNCTION__,
	     L4_ThreadNo(tid), (int) TAG_SYSLAB(tag),
	     (void *) L4_MsgWord(&msg, 0));
	
	//
	// Dispatch IPC according to protocol.
	//
	send = 1; /* In most cases we will want to send a reply */

	switch (TAG_SYSLAB(tag)) {
	case L4_PAGEFAULT:
	    // A pagefault occured. Dispatch to the pager
	    send = pager(tid, &msg);
	    // Generate Reply message
        L4_Set_MsgMsgTag(&msg, L4_Niltag);
        L4_MsgLoad(&msg); 
	    break;

	case L4_INTERRUPT:
            if(L4_ThreadNo(tid) == NSLU2_TIMESTAMP_IRQ) {
              handle_timer_timestamp_interrupt();
              L4_MsgClear(&msg);
              L4_MsgLoad(&msg);
            }  else if(L4_ThreadNo(tid) == NSLU2_TIMER1_IRQ) {
              //Got the timer interrupt
              handle_timer_interrupt();
              L4_MsgClear(&msg);
              L4_MsgLoad(&msg);
	    } else {
	      network_irq(&tid, &send);
            }
	    break;
	case SOS_SYSCALL_UNMAP_USER_ADDRESS_SPACE:
	    send = 0;
	    unmap_all();
	    break;
	case SOS_SYSCALL_FIND_RPC_THREAD:
	  //Add message to send the thread id of rpc server
	    dprintf(2, "find_rpc_thread called\n");
	    L4_Word_t threadNo = rpc_threadId.raw;
	    dprintf(2, "returning id %ld\n", threadNo);
	    L4_MsgClear(&msg);
	    L4_Set_MsgMsgTag(&msg, L4_Niltag);
  	    L4_MsgAppendWord(&msg, threadNo);
	    L4_MsgLoad(&msg);
	    break;
	case SOS_SYSCALL_REMOVE_TID_PAGE :
	  dprintf(2, "Unmap received for process kill");
	  send = 0;
	  unmap_process((L4_ThreadId_t) L4_MsgWord(&msg,0));
	  break;
	/* error? */
	default:
	    // Unknown system call, so we don't want to reply to this thread
	    sos_print_l4memory(&msg, L4_UntypedWords(tag) * sizeof(uint32_t));
	    send = 0;
            break;
	}
    }
}

//
// Main entry point - called by crt.
//


int
main (void)
{
    // Initialise initial sos environment
    libsos_init();
    // Initialse the memory frame table

    // Find information about available memory
    L4_Word_t low, high;
    sos_find_memory(&low, &high);
    dprintf(0, "Available memory from 0x%08lx to 0x%08lx - %luMB\n", 
	   low, high, (high - low) / ONE_MEG);
    //L4_Word_t new_high = low + HEAP_SIZE + 2 * PAGESIZE;
	dprintf(0, "initializing frame manager from 0x%08lx to 0x%08lx...\n", low + HEAP_SIZE, high);

    frame_init(low + HEAP_SIZE, high);

    dprintf(0, "initializing pager from 0x%08lx to 0x%08lx...\n", low + HEAP_SIZE, high);
    L4_Word_t new_low = pager_init((low + HEAP_SIZE), high);
    set_new_low(new_low);
    
    dprintf(0, "frame manager initialized\n");
    //L4_KDB_Enter("Woo");
    // Spawn the setup thread which completes the rest of the initialisation,
    // leaving this thread free to act as a pager and interrupt handler.
    (void) sos_thread_new(&init_thread, (L4_Word_t*) init_stack_address);
    // Spawn separate RPC thread
    rpc_threadId = sos_thread_new(&rpc_thread, (L4_Word_t*) rpc_stack_address);
    start_timer();
    /*dprintf(0,"%lx %lx %lx %lx\n",L4_Myself().raw,L4_Pager().raw,rpc_threadId.raw,init_id.raw);
    dprintf(0,"The timer status is %d\n",returnVal);
    dprintf(0,"The timer value is %llu\n",time_stamp());
    for(int i=1;i<100000;i++);
    dprintf(0,"The timer value is %u\n",time_stamp());
    for(int i=1;i<100000;i++);*/
    //stop_timer();
    syscall_loop(); // Enter the syscall loop
    /* Not reached */

    return 0;
}
