/*
 * Written by Vivek Shah
 */
#ifndef _HELPERS_H
#define _HELPERS_H

/*
 * Function to send data large enough for a message in one message by packing it in the message
 * registers. No sanity check is done to see if the data fits in one message
 */
extern int send_in_one_message(L4_ThreadId_t receiver, L4_Word_t MsgLabel, const char* data, int count);

#endif
