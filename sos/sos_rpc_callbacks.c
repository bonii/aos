#include "sos_rpc_callbacks.h"
#include "sos_rpc.h"
#include "l4.h"
#include "libsos.h"

#include <stdio.h>
#include <assert.h>

int filename_iterator;

filename_t files[MAX_FILES];

void nfs_readdir_callback(uintptr_t token, int status, int num_entries, struct nfs_filename* filenames, int next_cookie)
{
    dprintf(2, "nfs readdir callback\n");
    L4_ThreadId_t tid = (L4_ThreadId_t) token;
    for (int i = 0; i < num_entries && filename_iterator < MAX_FILES; i++)
    {
        // copy to our files-array
        int size = filenames[i].size;
        assert(size < FILE_NAME_SIZE);
        char* file = files[filename_iterator].chars;
        // initialize to 0
        for (int j = 0; j < FILE_NAME_SIZE; j++) 
            file[j] = 0;
        strncpy(file, filenames[i].file, size);
        dprintf(2, "nfs readdir encountered filename '%s' at position %d\n", file, filename_iterator);
        filename_iterator++;
    }
    if (next_cookie > 0 && filename_iterator < MAX_FILES)
    {
        dprintf(2, "need another readdir\n");
        nfs_readdir(&mnt_point, next_cookie, FILE_NAME_SIZE, nfs_readdir_callback, (uintptr_t) tid.raw);
    } else {
        dprintf(2, "readdir done... notifying rpc thread of threadid %lx.\n", tid.raw);
        L4_Msg_t msg;
        L4_MsgClear(&msg);
        L4_MsgLoad(&msg);
        L4_Send(tid);
        dprintf(2, "rpc thread notified.\n");
    }
}

void nfs_lookup_callback(uintptr_t token, int status, struct cookie* fh, fattr_t* attr)
{
    dprintf(2, "nfs lookup callback\n");
    L4_ThreadId_t tid = (L4_ThreadId_t) token;
    L4_Msg_t msg;
    L4_MsgClear(&msg);
    L4_Set_MsgMsgTag(&msg, L4_Niltag);
    dprintf(2, "status is %d\n", status);
    if (status == 0)
    {
        L4_Word_t st_type = (L4_Word_t) attr->type;
        L4_Word_t st_mode = (L4_Word_t) attr->mode;
        L4_Word_t st_size = (L4_Word_t) attr->size;
        L4_Word_t st_ctime = (L4_Word_t) (attr->ctime.seconds * 1.0e6 + attr->ctime.useconds)/1000;
        L4_Word_t st_atime = (L4_Word_t) (attr->atime.seconds * 1.0e6 + attr->atime.useconds)/1000;
        //printf("0x%06x 0x%lx 0x%06lx \n",(unsigned int)st_size, st_ctime, st_atime);

        L4_MsgAppendWord(&msg, st_type);
        L4_MsgAppendWord(&msg, st_mode);
        L4_MsgAppendWord(&msg, st_size);
        L4_MsgAppendWord(&msg, st_ctime);
        L4_MsgAppendWord(&msg, st_atime);
    } else {
        L4_MsgAppendWord(&msg, status);
    }
    L4_MsgLoad(&msg);
    dprintf(2, "nfs_lookup_callback sending to %lx...\n", tid.raw);
    L4_Send(tid);
    dprintf(2, "nfs_lookup_callback sent.\n");
}

void nfs_getcookie_callback(uintptr_t token, int status, struct cookie* fh, fattr_t* attr)
{
    L4_ThreadId_t tid = (L4_ThreadId_t) token;
    L4_Msg_t msg;
    L4_MsgClear(&msg);
    L4_Set_MsgMsgTag(&msg, L4_Niltag);
    L4_Set_MsgLabel(&msg, status);
    if (status == 0)
    {
        assert (fh != 0);
        // send the entire cookie back
        // this only works because sizeof(cookie) 
        // is a multiple of sizeof(L4_Word_t)
	L4_MsgAppendWord(&msg, (L4_Word_t)attr->size);
        for (int i = 0; i < sizeof(struct cookie)/sizeof(L4_Word_t); i++)
        {
            L4_MsgAppendWord(&msg, ((L4_Word_t*)fh)[i]);
        }
    }
    /*dprintf(0, "cookie in getcokie_callback: ");
    for (int i = 0 ; i < FHSIZE; i++)
    {
        dprintf(0, "%c", fh->data[i]);
    }
    dprintf(0, "\n");*/
    L4_MsgLoad(&msg);
    L4_Send(tid);
}

void nfs_read_callback(uintptr_t t, int status, fattr_t *attr, int bytes_read, char *data)
{
    // send the message back character by character
    int token = (int) t;
    dprintf(2, "nfs read callback with status %d, token %d\n", status, token);
    Token_Access_t* access = get_access_from_token(token);
    L4_ThreadId_t tid = access->tid;
    L4_Msg_t msg;
    if (status == 0 && bytes_read != 0)
    {
        for (int i = 0; i < bytes_read; i++)
        {
            L4_MsgClear(&msg);
            L4_Set_MsgMsgTag(&msg, L4_Niltag);
            L4_Set_MsgLabel(&msg, status);
            L4_Word_t word = 0;
            char* writeTo = (char*) &word;
            writeTo[0] = data[i];
            L4_MsgAppendWord(&msg, word);
            L4_MsgLoad(&msg);
            dprintf(2, "sending answer: from %lx to %lx : %c, word: %lx\n", L4_Myself().raw, tid.raw, data[i], L4_MsgWord(&msg, 0));
            L4_Send(tid);
            dprintf(2, "character sent back in nfs read callback\n");
            access->read_offset++;
            if (data[i] == '\n') break;
        }
        
    } else {
      if(status == 0 && bytes_read == 0) {
        status = END_OF_READ_FILE_STATUS;
      }
      dprintf(2,"Should terminate here");
      L4_MsgClear(&msg);
      L4_Set_MsgMsgTag(&msg, L4_Niltag);
      L4_Set_MsgLabel(&msg, status);
      L4_MsgLoad(&msg);
      L4_Send(tid);
    }
}

void nfs_write_callback(uintptr_t tokenparam, int status, fattr_t *attr) {
    int token = (int) tokenparam;
    dprintf(2, "nfs write callback with status %d, token %d\n", status, token);
    L4_Msg_t msg;
    Token_Access_t* access = get_access_from_token(token);
    L4_ThreadId_t tid = access->tid;
    int bytes_written = attr->size - access->write_offset;
    access->write_offset = attr->size;
    L4_MsgClear(&msg);
    L4_Set_MsgMsgTag(&msg, L4_Niltag);
    L4_Set_MsgLabel(&msg, status);
    L4_MsgAppendWord(&msg, bytes_written);
    L4_MsgLoad(&msg);
    L4_Send(tid);
}
