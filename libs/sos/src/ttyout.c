/****************************************************************************
 *
 *      $Id:  $
 *
 *      Description: Simple milestone 0 code.
 *      		     Libc will need sos_write & sos_read implemented.
 *
 *      Author:      Ben Leslie
 *
 ****************************************************************************/

#include <stdarg.h>
#include <assert.h>
#include <stdlib.h>
#include <sos.h>
#include "ttyout.h"

#include <l4/types.h>
#include <l4/ipc.h>
#include <l4/kdebug.h>
#include <l4/thread.h>

#include "../../../helpers/ipc_defs.h"

static L4_ThreadId_t sSystemId;
static fildes_t console;
void
ttyout_init(void) {
	/* Perform any initialisation you require here */
	sSystemId = L4_Pager();
	console = open("console", O_RDWR);
	assert (console >= 0);
}

/*
 * Helper function to send message to pager to unmap all L4 pages(milestone 2)
 */
void
sos_debug_flush(void)
{
    // Send a syscall to the root thread, which unmaps the user address space.
	L4_Msg_t msg;
	L4_MsgClear(&msg);
	L4_Set_MsgLabel(&msg, MAKETAG_SYSLAB(SOS_SYSCALL_UNMAP_USER_ADDRESS_SPACE));
	L4_MsgLoad(&msg);
	L4_MsgTag_t tag = L4_Send(sSystemId);
	if (L4_IpcFailed(tag)) {
	    printf("Error sending unmap address space syscall.");
	}
}

/*
 * Helper function which sends a large data by breaking it into identical small message and
 * sending it
 */
size_t
send_message_with_info(L4_Word_t info, L4_ThreadId_t receiver, L4_Word_t MsgLabel, const char* data, size_t count, L4_ThreadId_t receiveReplyFrom)
{
	// send to syscall in chunks of 5 words 
	// so that we use all 6 ARM registers
	//for (i = position; i < count;) // well looks like this is not what position is for.
	int i = 0;
	int bytes_sent = 0;
	for (i = 0; i < count;) // i incremented in the loop 
	{
	   	L4_Msg_t msg;
		L4_MsgClear(&msg);
		L4_Set_MsgLabel(&msg, MAKETAG_SYSLAB(MsgLabel));
		L4_MsgAppendWord(&msg, info);
		// add the words to the message
                int buffersize = 0;
		for (int j = 0; j < WORDS_SENT_PER_IPC-2 && i < count; j++)
		{
			// initialize to 0 so we send 0-chars if the message 
			// is not word-padded
			L4_Word_t word = 0;
			// add bytes one by one
		    	char* writeTo = (char*) &word;
		    	for (int k=0; k < sizeof(L4_Word_t) && i < count; k++)
		    	{
				*(writeTo++) = data[i++];
                                buffersize++;
			}
			L4_MsgAppendWord(&msg, word);
		}
                L4_MsgAppendWord(&msg,buffersize);
		L4_MsgLoad(&msg);
                L4_MsgTag_t tag;
		if(L4_ThreadNo(receiveReplyFrom)==L4_ThreadNo(L4_nilthread)) {
		    tag = L4_Call(receiver);
                } else {
                    L4_Send(receiver);
                    tag = L4_Receive(receiveReplyFrom);
		} 
		if (L4_IpcFailed(tag)) {
		    // FIXME: actually useful debug message
		    L4_KDB_PrintChar('E');
		    L4_KDB_PrintChar('S');
		    L4_KDB_PrintChar('M');
		    break;
		}
		L4_MsgStore(tag, &msg);
		assert(tag.X.u == 1);
		int bytes_sent_new = L4_MsgWord(&msg, 0);
		if (bytes_sent_new < 0)
		{
		    bytes_sent = -1; break; // completely unsuccessful write
		} else {
		    bytes_sent += bytes_sent_new;
		}
	}
	return (bytes_sent >= (int)count) ? count : bytes_sent;
}

size_t
sos_write(const void *vData, long int position, size_t count, void *handle)
{
	size_t i;
	const char *realdata = vData;
	// send over serial
	/*for (i = 0; i < count; i++)
	  L4_KDB_PrintChar(realdata[i]);*/
#if 1
    i = write(console, realdata, count);
#endif
	return i;
}

size_t
sos_read(void *vData, long int position, size_t count, void *handle)
{
	size_t i;
	char *realdata = vData;
	/*	for (i = 0; i < count; i++) // Fix this to use your syscall
		realdata[i] = L4_KDB_ReadChar_Blocked();*/
	i = read(console,realdata,count);
	return i;
}

/*void
abort(void)
{
L4_KDB_Enter("sos abort()ed");*/
//	while(1); /* We don't return after this */
/*}

  void _Exit(int status) { abort(); }*/

