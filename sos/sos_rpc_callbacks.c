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

void process_create_lookup_callback(uintptr_t token, int status, struct cookie* fh, fattr_t* attr) {
  struct token_process_t *token_val = (struct token_process_t *) token;
  L4_ThreadId_t tidval = token_val -> creating_process_tid;
  int pageindex = token_val -> process_table_index;
  dprintf(0,"status value is %d tid %lx %d %p",status,tidval.raw,pageindex,token_val);
  L4_Msg_t msg;
  int errorFlag = 0;
  if(status != 0) {
    errorFlag = 1;
  }
  if(!errorFlag) {
    char *elf_file = malloc(attr->size);
    if(elf_file) {
      struct cookie *filecookie = (struct cookie *) malloc(sizeof(struct cookie));
      memcpy(filecookie , fh, sizeof(struct cookie));
      elf_file[0] = 0;
      token_val -> elf_file = elf_file;
      token_val -> data_read = 0;
      token_val -> file_size = attr -> size;
      token_val -> fh = filecookie;

      int val = nfs_read(fh,0,NFS_READ_SIZE,process_create_read_callback,(uintptr_t) token_val);
      dprintf(0,"Issued nfs read with %d\n",val);
    } else {
      errorFlag = 1;
    }
  }

  if(errorFlag) {
    L4_MsgClear(&msg);
    L4_MsgAppendWord(&msg,-1);
    L4_MsgLoad(&msg);
    L4_Send(token_val -> creating_process_tid);
    free(token_val);
  }
}

void process_create_read_callback(uintptr_t token, int status, fattr_t *attr, int bytes_read, char *data) {
  //dprintf(0,"Received process_create read callback with status %d\n",status);
  int errorFlag = 0,completedFlag=0;
  L4_Msg_t msg;
  struct token_process_t *token_val = (struct token_process_t *) token;
  if(status == 0) {
    memcpy(token_val -> elf_file + token_val -> data_read,data,bytes_read);
    token_val -> data_read += bytes_read;
    //L4_KDB_Enter("blu");
    //dprintf(0,"Data read %d size %d\n",token_val -> data_read,token_val -> file_size);
    if(token_val -> data_read < token_val -> file_size) {
      nfs_read(token_val -> fh,token_val -> data_read,NFS_READ_SIZE,process_create_read_callback,token);
    } else {
      token_val -> elf_file [token_val -> file_size] = 0;
      int checkFailed =  elf_checkFile((void *)token_val -> elf_file);
      dprintf(0,"Finished process_create read callbacks %d\n",checkFailed);
      //We have read the file into elf_file now we need to check the elf format
      if(checkFailed != 0) {
	errorFlag = 1;
      } else {
	L4_Word_t entry_point = (L4_Word_t) elf_getEntryPoint((void *)token_val -> elf_file);
	dprintf(0,"We are here %lx\n",entry_point);
	L4_ThreadId_t newtid = sos_thread_create(token_val -> process_table_index + 5);
	dprintf(0,"New tid val is %lx\n",newtid.raw);
	errorFlag = load_code_segment_virtual(token_val -> elf_file,newtid);
	dprintf(0,"Are we here ?");
	if(errorFlag < 0)
	  errorFlag = 1;
	else
	  errorFlag = 0;
	if(!errorFlag) {
	  L4_KDB_Enter("Foo bam");
	  //Activate the tid and we are done
	  dprintf(0,"Activating thread\n");
	  dprintf(0,"%lx %lx\n",L4_Myself().raw,L4_Pager().raw);
	  L4_ThreadId_t new_tid_val = sos_thread_activate(newtid,L4_Myself(),(void *) entry_point,(void *)stack_address);
	  dprintf(0,"Here ?");
	  process_table_add_creation_entry(token_val -> process_table_index,newtid,1);
	  dprintf(0,"tid value is %lx\n",new_tid_val.raw);
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
    return;
  }
  L4_MsgClear(&msg);
  if(errorFlag) {
    L4_MsgAppendWord(&msg, -1);
    process_table_add_creation_entry(token_val -> process_table_index,L4_nilthread,0);
  } else {
    L4_MsgAppendWord(&msg, token_val -> process_table_index);
  }
  L4_MsgLoad(&msg);
  L4_Send(token_val -> creating_process_tid);
  free(token_val -> elf_file);
  free(token_val -> fh);
  free(token_val);
}
