#include <l4/ipc.h>
#include "helpers.h"
#include "../helpers/ipc_defs.h"

/*
 * Function to send data large enough for a message in one message by packing it in the message
 * registers. No sanity check is done to see if the data fits in one message
 */
int send_in_one_message(L4_ThreadId_t receiver, L4_Word_t MsgLabel, const char* data, int count)
{
  	L4_Msg_t msgx;
	L4_MsgClear(&msgx);
	L4_Set_MsgLabel(&msgx, MAKETAG_SYSLAB(MsgLabel));
	for (int i = 0; i < count;) // i incremented in the loop 
	{
		L4_Word_t word = 0;
		// add bytes one by one
	    char* writeTo = (char*) &word;
	    for (int k = 0; k < sizeof(L4_Word_t) && i < count; k++)
	    {
			*(writeTo++) = data[i++];
		}
		L4_MsgAppendWord(&msgx, word);

	}
	L4_MsgLoad(&msgx);
	L4_MsgTag_t tagx = L4_Send(receiver);
	if (L4_IpcFailed(tagx))
	{
	    return -1;
	}
	return 0;
}
