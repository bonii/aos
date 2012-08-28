/****************************************************************************
 *
 *      $Id:  $
 *
 *      Description: Simple milestone 0 test.
 *
 *      Author:			Godfrey van der Linden
 *      Original Author:	Ben Leslie
 *      Modified by:        Vivek Shah
 *
 ****************************************************************************/

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include <l4/types.h>

#include <l4/ipc.h>
#include <l4/message.h>
#include <l4/thread.h>

#include "ttyout.h"

#include "../helpers/ipc_defs.h"

#define NPAGES 128

/* called from pt_test */
static void
do_pt_test( char *buf )
{
    int i;

    /* set */
    for(i = 0; i < NPAGES; i += 4)
    buf[i * 1024] = i;

    /* flush */
    printf("debug flush\n");
    sos_debug_flush();

    /* check */
    for(i = 0; i < NPAGES; i += 4)
    assert(buf[i*1024] == i);
}

static void rpc_test(void)
{
    printf("Testing rpc\n");
    L4_Msg_t msg;
    L4_MsgClear(&msg);
    L4_Set_MsgLabel(&msg,MAKETAG_SYSLAB(SOS_SYSCALL_FIND_RPC_THREAD));
    L4_MsgLoad(&msg);
    L4_MsgTag_t tag;
    L4_ThreadId_t rootThread = L4_Pager();
    L4_Accept(L4_UntypedWordsAcceptor);
    //printf("sending.\n");
    tag = L4_Call(rootThread);
   	L4_MsgStore(tag, &msg); 
    L4_Word_t threadNo = L4_MsgWord(&msg, 0);
    printf("%lx\n", threadNo);

}

static void
pt_test( void )
{
    /* need a decent sized stack */
    printf ("testing stack...\n");
    char buf1[NPAGES * 1024], *buf2 = NULL;

    /* check the stack is above phys mem */
    assert((void *) buf1 > (void *) 0x2000000);

    /* stack test */
    do_pt_test(buf1);
    printf("Finished testing stack\n");
    printf ("testing heap...\n");
    /* heap test */

    buf2 = malloc(NPAGES * 1024);
    assert(buf2);

    /* check the heap is above phys mem */
    assert((void *) buf2 > (void *) 0x2000000);
    
    do_pt_test(buf2);
    sos_debug_flush();
    free(buf2);
    printf("Finished testing heap\n");
}

// Block a thread forever
static void
thread_block(void)
{
    L4_Msg_t msg;

    L4_MsgClear(&msg);
    L4_MsgTag_t tag = L4_Receive(L4_Myself());

    if (L4_IpcFailed(tag)) {
	printf("blocking thread failed: %lx\n", tag.raw);
	*(char *) 0 = 0;
    }
}

int main(void)
{
    L4_ThreadId_t myid;
    
    /* initialise communication */
    ttyout_init();
    
    //myid = L4_Myself();
    do {
	printf("task:\tHello L4, I'm\t0x%lx! :)\n", L4_ThreadNo(myid));
    //malloc(4096*14);
    if (0) pt_test();
    rpc_test();
	thread_block();
	// sleep(1);	// Implement this as a syscall
    } while(1);
    
    return 0;
}
