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

#include "ttyout.h"

#include <l4/types.h>
#include <l4/ipc.h>
#include <l4/kdebug.h>
#include <l4/thread.h>

#include "../helpers/ipc_defs.h"

static L4_ThreadId_t sSystemId;

void
ttyout_init(void) {
	/* Perform any initialisation you require here */
	sSystemId = L4_Pager();
}

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

size_t
sos_write(const void *vData, long int position, size_t count, void *handle)
{
	size_t i;
	const char *realdata = vData;
	// send over serial
	for (i = 0; i < count; i++)
		L4_KDB_PrintChar(realdata[i]);

	// send to syscall in chunks of 5 words 
	// so that we use all 6 ARM registers
	//for (i = position; i < count;) // well looks like this is not what position is for.
	for (i = 0; i < count;) // i incremented in the loop 
	{
	   	L4_Msg_t msg;
		L4_MsgClear(&msg);
		L4_Set_MsgLabel(&msg, MAKETAG_SYSLAB(SOS_SYSCALL_SERIAL_SEND));
		// add the words to the message
		for (int j = 0; j < WORDS_SENT_PER_IPC && i < count; j++)
		{
			// initialize to 0 so we send 0-chars if the message 
			// is not word-padded
			L4_Word_t word = 0;
			// add bytes one by one
		    	char* writeTo = (char*) &word;
		    	for (int k = 0; k < sizeof(L4_Word_t) && i < count; k++)
		    	{
				*(writeTo++) = realdata[i++];
			}
			L4_MsgAppendWord(&msg, word);
		}

		L4_MsgLoad(&msg);
		L4_MsgTag_t tag = L4_Send(sSystemId);
		if (L4_IpcFailed(tag)) {
		    // FIXME: actually useful debug message
		    L4_KDB_PrintChar('E');
		    L4_KDB_PrintChar('S');
		    L4_KDB_PrintChar('M');
		    break;
		}
	}

	return i;
}

size_t
sos_read(void *vData, long int position, size_t count, void *handle)
{
	size_t i;
	char *realdata = vData;
	for (i = 0; i < count; i++) // Fix this to use your syscall
		realdata[i] = L4_KDB_ReadChar_Blocked();
	return count;
}

void
abort(void)
{
	L4_KDB_Enter("sos abort()ed");
	while(1); /* We don't return after this */
}

void _Exit(int status) { abort(); }

