/*
 * Callback functions for the rpc thread invoked for nfs calls
 * Written by Vivek Shah
 */

#include "sos_rpc_callbacks.h"
#include "sos_rpc.h"
#include "l4.h"
#include "libsos.h"
#include <stdlib.h>
#include <stdio.h>
#include <l4/types.h>
#include <assert.h>
#include <elf/elf.h>
#include <elf/elf32.h>
#include "pager.h"

int filename_iterator;
static L4_Word_t stack_address = 0x7750000;
filename_t files[MAX_FILES];

/*
 * Callback function for nfs readdir. It reads directory contents incrementally issuing nfs 
 * readdirs
 */
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

/*
 * Callback function for nfs lookup, loads the file attributes if found
 */
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
      //Load the attributes if file found
        L4_Word_t st_type = (L4_Word_t) attr->type;
        L4_Word_t st_mode = (L4_Word_t) attr->mode;
        L4_Word_t st_size = (L4_Word_t) attr->size;
        L4_Word_t st_ctime = (L4_Word_t) (attr->ctime.seconds * 1.0e6 + attr->ctime.useconds)/1000;
        L4_Word_t st_atime = (L4_Word_t) (attr->atime.seconds * 1.0e6 + attr->atime.useconds)/1000;
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

/*
 * Callback function to get_file_handle, loads the cookie which is to be used for subsequent
 * nfs calls
 */
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
    L4_MsgLoad(&msg);
    L4_Send(tid);
}

/*
 * Callback function for nfs read, it replies to the thread waiting for the read bytes character
 * by character
 */
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
      //If complete file has been read
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

/*
 * Callback function for nfs write 
 */
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

/*
 * Callback function for process creation elf file lookup
 */
void process_create_lookup_callback(uintptr_t token, int status, struct cookie* fh, fattr_t* attr) {
  struct token_process_t *token_val = (struct token_process_t *) token;
  L4_ThreadId_t tidval = token_val -> creating_process_tid;
  int pageindex = token_val -> process_table_index;
  dprintf(2,"status value is %d tid %lx %d %p",status,tidval.raw,pageindex,token_val);
  L4_Msg_t msg;
  int errorFlag = 0;
  if(status != 0 || !(attr -> mode & FM_EXEC)) {
    errorFlag = 1;
  }
  if(!errorFlag) {
    //Allocate elffile memory
    char *elf_file = malloc(attr->size);
    if(elf_file) {
      //Copy the cookie to be used for nfs read
      struct cookie *filecookie = (struct cookie *) malloc(sizeof(struct cookie));
      memcpy(filecookie , fh, sizeof(struct cookie));
      elf_file[0] = 0;
      //Load the token
      token_val -> elf_file = elf_file;
      token_val -> data_read = 0;
      token_val -> file_size = attr -> size;
      token_val -> fh = filecookie;
      //read the file
      int val = nfs_read(fh,0,NFS_READ_SIZE,process_create_read_callback,(uintptr_t) token_val);
      dprintf(2,"Issued nfs read with %d\n",val);
    } else {
      errorFlag = 1;
    }
  }

  if(errorFlag) {
    //Undo the process table entry which was filled with root process tid to free it
    process_table_add_creation_entry(token_val -> process_table_index,L4_nilthread,0);
    L4_MsgClear(&msg);
    L4_MsgAppendWord(&msg,-1);
    L4_MsgLoad(&msg);
    L4_Send(token_val -> creating_process_tid);
    free(token_val);
  }
}

/*
 * Callback function for process create nfs read of the elf file
 */
void process_create_read_callback(uintptr_t token, int status, fattr_t *attr, int bytes_read, char *data) {
  int errorFlag = 0,completedFlag=0;
  L4_Msg_t msg;
  struct token_process_t *token_val = (struct token_process_t *) token;
  if(status == 0) {
    //Copy the data read to the elf file
    memcpy(token_val -> elf_file + token_val -> data_read,data,bytes_read);
    token_val -> data_read += bytes_read;

    //Check if the file has been read else issue the next read
    if(token_val -> data_read < token_val -> file_size) {
      nfs_read(token_val -> fh,token_val -> data_read,NFS_READ_SIZE,process_create_read_callback,token);
    } else {
      //Finished reading the file successfully
      token_val -> elf_file [token_val -> file_size] = 0; //Null terminate the file just in case
      int checkFailed =  elf_checkFile((void *)token_val -> elf_file);
      dprintf(2,"Finished process_create read callbacks %d\n",checkFailed);
      //We have read the file into elf_file now we need to check the elf format
      if(checkFailed != 0) {
	errorFlag = 1;
      } else {
	L4_Word_t entry_point = (L4_Word_t) elf_getEntryPoint((void *)token_val -> elf_file);
	//Create a new task but do not activate it
	L4_ThreadId_t newtid = sos_thread_create(token_val -> process_table_index + 5);
	//load the code segment(entire elf file) into the page frames
	//If there is not enough space in the frame table then do not load the process
	errorFlag = load_code_segment_virtual(token_val -> elf_file,newtid);

	if(errorFlag < 0) {
	  errorFlag = 1;
	  //We need to delete the task so that it does not hang around
	  sos_delete_task(newtid,L4_Myself());
	} else {
	  errorFlag = 0;
	}
	if(!errorFlag) {
	  dprintf(2,"Activating thread\n");
	  L4_ThreadId_t new_tid_val = sos_thread_activate(newtid,L4_Myself(),(void *) entry_point,(void *)stack_address);
	  if(new_tid_val.raw < 0)
	    errorFlag = 1;
	  else {
	    //Fill out the process table with the tid and the creation time
	    process_table_add_creation_entry(token_val -> process_table_index,newtid,1);
	  }
	}
      }
      completedFlag = 1;
    }
  } else {
    errorFlag = 1;
  }
  
  //We dont send a message unless we have an error in one of the callbacks or we finished 
  //processing the last callback
  if(!errorFlag && !completedFlag) {
    //There are more nfs reads callbacks to be handled
    return;
  }
  L4_MsgClear(&msg);
  if(errorFlag) {
    L4_MsgAppendWord(&msg, -1);
    //Undo the process table entry which was filled with root process tid to free it
    process_table_add_creation_entry(token_val -> process_table_index,L4_nilthread,0);
  } else {
    L4_MsgAppendWord(&msg, token_val -> process_table_index);
  }
  L4_MsgLoad(&msg);
  L4_Send(token_val -> creating_process_tid);
  //Free the malloced memory
  free(token_val -> elf_file);
  free(token_val -> fh);
  free(token_val);
}
