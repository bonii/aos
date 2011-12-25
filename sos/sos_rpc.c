#include "sos_rpc.h"

#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include "libsos.h"

#include <serial.h>
#include "sos_rpc_callbacks.h"
#include "helpers.h"
#include <clock.h>
#include <elf/elf.h>

struct serial * serial_port = 0;

static L4_ThreadId_t rpc_threadId;
static Token_Descriptor_t token_table[MAX_TOKENS];
static L4_Msg_t msg;
static L4_MsgTag_t tag;
static L4_ThreadId_t tid;
static read_listener_t console_listeners[MAX_CONSOLE_READERS];
static process_control_block_t process_table[MAX_PROCESSES];
static int BUFFER_LOCK;
static L4_ThreadId_t any_process_list[MAX_PROCESSES];


static char read_buffer[BUFFER_SIZE];

static int get_empty_token(void)
{
    int i;
    for (i = 0; i < MAX_TOKENS; i++)
    {
        if (token_table[i].read_count == 0 && token_table[i].write_count == 0)
        {
            break;
        }
    }
    return i;
}

static int get_empty_pid(void)
{
    int i;
    for (i = 0; i < MAX_PROCESSES; i++)
    {
      if (L4_ThreadNo(process_table[i].tid) == L4_ThreadNo(L4_nilthread))
        {
            break;
        }
    }
    return i;
}

static int get_pid_from_table(L4_ThreadId_t tid_search) {
    int i;
    for (i = 0; i < MAX_PROCESSES; i++)
    {
      if (L4_ThreadNo(process_table[i].tid) == L4_ThreadNo(tid_search))
        {
            break;
        }
    }
    return i;
}

void update_process_table_size(L4_ThreadId_t tid_of_page,unsigned increase) {
  int index = get_pid_from_table(tid_of_page);
  if(index >= 0 && index < MAX_PROCESSES) {
    if(increase) {
      process_table[index].size += 1;
    } else {
      process_table[index].size -= 1;
    }
    dprintf(0,"Process table size of pid %lx is %d",process_table[index].tid.raw,process_table[index].size);
  }
  
}

static int get_index_from_token(int token) {
  return (int) (token/(MAX_ACCESSES));
}

static int get_access_index_from_token (int token)
{
    return token % MAX_ACCESSES;
}

Token_Access_t* get_access_from_token(int token) {
    int index = get_index_from_token(token);
    Token_Descriptor_t* desc = &(token_table[index]);
    return &(desc->accesses[get_access_index_from_token(token)]);
}

static void get_serial_port_input(struct serial * serial_port,char input) {
    dprintf(2, "got input token %c\n", input);
    char temp[2];
    int i = 0;
    temp[0] = input;
    temp[1] = 0;
    L4_Word_t word = 0;
    char* writeTo = (char*) &word;
    while (BUFFER_LOCK) {
        ;
    }
    //Now buffer it
    BUFFER_LOCK = 1;
    if(strlen(read_buffer) + 1 == BUFFER_SIZE) {
      //We need to move things around in the buffer
      //Since we only get one character we need to
      //remove the first character
      for(i=0;i<BUFFER_SIZE;i++) {
	read_buffer[i] = read_buffer[i+1];
      }
    } 
    //Copy character to the buffer  
    strcat(read_buffer,temp);

    //Take the lock
    int buffer_index_flushed = -1;
    for(i=0;i<strlen(read_buffer);i++) {
      dprintf(2,"%c .. buffer",read_buffer[i]);
      for(int j=0;j<MAX_CONSOLE_READERS;j++) {
	if(L4_ThreadNo(console_listeners[j].tid) != L4_ThreadNo(L4_nilthread)
	   && console_listeners[j].number_bytes_left > 0 
	   && console_listeners[j].read_enabled) {

	  //L4_MsgTag_t tag;
	        L4_Msg_t msg1;
		L4_MsgClear(&msg1);
		L4_Set_MsgMsgTag(&msg1, L4_Niltag);
		L4_Set_MsgLabel(&msg1, 0);
		writeTo[0] = read_buffer[i];
		L4_MsgAppendWord(&msg1, word);
		L4_MsgLoad(&msg1);
		dprintf(2,"Sending message to %lx %c\n",L4_ThreadNo(console_listeners[j].tid)
			,read_buffer[i]);
		L4_Send(console_listeners[j].tid);
		dprintf(2,"After message sent \n:");
		console_listeners[j].number_bytes_left--;
		if(read_buffer[i] == '\n' || console_listeners[j].number_bytes_left == 0) {
		  console_listeners[j].tid = L4_nilthread;
		  console_listeners[j].number_bytes_left = 0;
		}
		buffer_index_flushed = i;
	}
      }
    }
      //Flush the buffer
    int counter = 0;
    for(i=buffer_index_flushed;i<BUFFER_SIZE && i!= -1;i++,counter++) {
      read_buffer[counter] = read_buffer[buffer_index_flushed+counter+1];
      
    }
    read_buffer[counter]=0;
    dprintf(2,"read buffer is %s\n",read_buffer);
    //Release lock
    BUFFER_LOCK = 0;
}

static int get_file_handle(char* filename, struct cookie* writeTo, int* size)
{
    nfs_lookup(&mnt_point, filename, nfs_getcookie_callback, (uintptr_t) L4_Myself().raw);
    // go to sleep
    L4_MsgTag_t new_tag = L4_Receive(L4_Pager());
    L4_Msg_t new_msg;
    dprintf(2, "rpc thread woken up from nfs_lookup_callback_open\n");
    L4_MsgStore(new_tag, &new_msg);
    int status = L4_Label(new_tag);
    *size = L4_MsgWord(&new_msg, 0);
    for (int i = 0; i < sizeof(struct cookie)/sizeof(L4_Word_t); i++)
    {
        ((L4_Word_t*)writeTo)[i] = L4_MsgWord(&new_msg, i+1);
    }

    dprintf(0, "\n");
    return status;
}

static int sos_rpc_get_token(void)
{
    dprintf(0, "SOS_RPC_GET_TOKEN called.\n");
    assert(tag.X.u == FILE_NAME_WORDS + 1);
    char tokenData[FILE_NAME_MODE_SIZE];
    L4_MsgGet(&msg, (L4_Word_t*)tokenData);
    fmode_t mode = (fmode_t) tokenData[FILE_NAME_SIZE];
    //for (int i = 0; i < FILE_NAME_MODE_SIZE; i++ ) dprintf(0, "%c ", tokenData[i]);
    char* filename = (char*) tokenData;

    dprintf(2, "filename: %s\n", filename);
    tokenData[FILE_NAME_SIZE] = 0; // 0-terminate the filename
    
    dprintf(2, "looking for a corresponding token.\n");
    int found = -1;
    for (int i = 0; i < MAX_TOKENS; i++)
    {
        if (strcmp(filename, token_table[i].filename) == 0 && 
        (token_table[i].read_count != 0 || token_table[i].write_count != 0))
        {
            found = i; break;
        }
    }
    dprintf(2, "found token %ld\n", found);
    
    char console = (strcmp(filename, "console") == 0);
    uint8_t max_readers = 0;
    uint8_t max_writers = 0;
    if (console)
    {
        max_readers = MAX_CONSOLE_READERS;
        max_writers = MAX_ACCESSES;
    } else {
        max_readers = MAX_ACCESSES;
        max_writers = 1;
    }
    int token = 0;
    if (found < 0)
    {
        found = get_empty_token();

        if (found >= MAX_TOKENS)
        {
            token = -1;
            found = -1;
        } else {
            strcpy(token_table[found].filename, filename);
        }
    }
    dprintf(2, "after get_empty_token: %ld\n", found);
    dprintf(2, "mode: %ld\n", mode);

    if (token_table[found].access_count == MAX_ACCESSES-1)
    {
        // accesses full
        dprintf(2, "accesses full for token %d\n", token);
        token = -1;
    }
    char read_encountered = 0;
    int access_idx = 0;
    Token_Access_t* access = 0;
    if (token != -1)
    {
        dprintf(2,"Before switch token value is %d\n",token);
        token = -1;
        switch(mode)
        {
            case O_RDONLY:
            if (token_table[found].read_count < max_readers)
            {
                token_table[found].read_count++;
                access_idx = token_table[found].access_count++;
                token = found;
                read_encountered = 1;
            } else {
              dprintf(2,"We are here max readers %d read count %d\n",max_readers,token_table[found].read_count);
            }
            break;
            case O_WRONLY:
            if (token_table[found].write_count < max_writers)
            {
                token_table[found].write_count++;
                access_idx = token_table[found].access_count++;
                token = found;
            }
            break;
            case O_RDWR:
            if (token_table[found].write_count < max_writers && token_table[found].read_count < max_readers)
            {

                token_table[found].read_count++;
                token_table[found].write_count++;
                access_idx = token_table[found].access_count++;
                token = found;
                read_encountered = 1;
            }
            break;
            default:
            break;
        }
        dprintf(2,"Token value after case is %d\n",token);
        if (token != -1)
        {
            access = &(token_table[found].accesses[access_idx]);
            access->tid = tid;
            dprintf(2,"Setting access tid %lx for token %d\n",access->tid.raw,token);
            access->write_offset = 0;
            access->read_offset = 0;
            access->mode = mode;

	    //Set the entry in the process table
	    int index_val = get_pid_from_table(tid);
	    if(index_val >= 0 && index_val < MAX_PROCESSES) {
	      for(int i=0;i<MAX_TOKENS;i++) {
		if(process_table[index_val].token_table[i] == -1)
		  process_table[index_val].token_table[i] = token;
	      }
	    }
	    
        }
    }
    if(console && read_encountered && token != -1) {
        if(serial_port == 0) {
            serial_port = serial_init();
            //The BUFFER_LOCK is unlocked
            BUFFER_LOCK = 0;
            //Initialise the buffer
            read_buffer[0] = 0;
            serial_register_handler(serial_port,get_serial_port_input);
        }
    } else if(!console && token != -1) {
        // we delegate this problem to the nfs_lookup
        struct cookie fh;
	int size;
        int status = get_file_handle(filename, &fh, &size);
        if (status != 0)
        {
            sattr_t attributes;
            attributes.mode = mode;
            attributes.size = 0;
            // we need to create the file
            nfs_create(&mnt_point, filename, &attributes, nfs_getcookie_callback, (uintptr_t) L4_Myself().raw);
            // go to sleep
            L4_Msg_t new_msg;
            L4_MsgTag_t new_tag = L4_Receive(L4_Pager());
            status = L4_Label(new_tag);
            dprintf(2, "rpc thread woken up from nfs_create_callback, status is %d\n", status);
            L4_MsgStore(new_tag, &new_msg);
            L4_MsgGet(&new_msg, (L4_Word_t*) &fh);
        } else {
            access->write_offset = size;
	    access->max_read_offset = size;
	}
    }
    L4_MsgClear(&msg);
    if (token >= 0)
    {
        token *= MAX_ACCESSES;
        token += access_idx;
    }
    L4_MsgAppendWord(&msg, token);
    L4_MsgLoad(&msg);
    dprintf(2, "message for SOS_GET_TOKEN assembled: token %d\n", token);
    return 1;
}

static char has_access(L4_ThreadId_t tid, int token, fmode_t mode)
{
    Token_Access_t* access = get_access_from_token(token);
    return (access->tid.raw == tid.raw || ((access->mode|mode) != 0));
}

static int sos_rpc_write(void)
{
    L4_Word_t realdata[WORDS_SENT_PER_IPC];
    L4_MsgGet(&msg, realdata);
    // first word is the file descriptor token
    int token = realdata[0];
    int token_index = get_index_from_token(token);
    //dprintf(0, "writing to token %ld\n", token);
    // find if the file is correctly opened for writing
    Token_Descriptor_t* file = &token_table[token_index];
    Token_Access_t* access = get_access_from_token(token); 
    int bytes_sent = -1;
    int send = 1;
    if (!has_access(tid, token, FM_WRITE))
    {
        dprintf(2, "file %s is not opened for writing by %ld\n", file->filename, L4_ThreadNo(tid));
    } else {
    //dprintf(0, "payload is \n---\n%s\n---\n", (char*)(&realdata[1]));
        char* filename = token_table[token_index].filename;
        char console = (strcmp(filename, "console") == 0);
        int bytes_without_length_info = (int)realdata[tag.X.u-1];
        if (console)
        {
            //dprintf(0, "writing to console: '%s'\n", (char*)(&realdata[1]));
            // try to send count bytes
            //dprintf(0, "%lx", serial_port);
            if(serial_port == 0) serial_port = serial_init();
            bytes_sent = serial_send(serial_port, (char*)(&realdata[1]), bytes_without_length_info);
            if (bytes_sent < bytes_without_length_info)
            {
                dprintf(2, "could only send %d/%d bytes over network", bytes_sent, bytes_without_length_info);
            }
            //send = 1;
        } else {
            dprintf(2, "not writing to console...\n");
            // TODO: Write to a file (not yet implemented)
            //We need to get the file handle
            struct cookie fh;
	    int size;
            get_file_handle(token_table[token_index].filename, &fh, &size);
            nfs_write(&fh, access->write_offset, bytes_without_length_info,(char *)(&realdata[1]), nfs_write_callback, (uintptr_t) token);
            dprintf(2,"After nfs write");
            send = 0;
        }
    }
    
    if(send) {
        L4_MsgClear(&msg);
        L4_MsgAppendWord(&msg, bytes_sent);
        L4_MsgLoad(&msg);
    }
    //dprintf(0, "message for SOS_RPC_WRITE assembled.");
    return (send);
}

static int close_file_descriptor(int token) {
    int index = get_index_from_token(token);
    int found = -1;
    int access_index = get_access_index_from_token(token);
    Token_Access_t* access = get_access_from_token(token);
    //If it is a console reader deregister it
    dprintf(2,"IReplying to tid %lx",L4_ThreadNo(tid));
    while(BUFFER_LOCK) {
      ;
    }
    BUFFER_LOCK = 1;
    if(strcmp(token_table[index].filename,"console") == 0) {
      for(int i=0;i<MAX_CONSOLE_READERS;i++) {
        if(L4_ThreadNo(console_listeners[i].tid) == L4_ThreadNo(access->tid)) {
          console_listeners[i].tid = L4_nilthread;
          console_listeners[i].read_enabled = 0;
          console_listeners[i].number_bytes_left = 0;
        }
      }
    }
    dprintf(2,"IReplying to tid %lx",L4_ThreadNo(tid));
    BUFFER_LOCK = 0;
    if(access_index < MAX_ACCESSES && access->tid.raw == tid.raw) {
        found = 0;
        //decrement relevant counters
        token_table[index].access_count--;
        if (access->mode & FM_READ)
            token_table[index].read_count--;
        if (access->mode & FM_WRITE)
            token_table[index].write_count--;
        //compact the table
        for(int j= access_index; j < token_table[index].access_count - 1 ; j++) {
            token_table[index].accesses[j] = token_table[index].accesses[j+1];
        }
    }
    dprintf(2,"Replying to tid %lx",L4_ThreadNo(tid));
    return found;
}

static int sos_rpc_release_token(void)
{
    dprintf(2,"Release token received");
    int token = (int) L4_MsgWord(&msg,0);
    //Get the token number from the message which is my index into the datastructure
    //Construct the message and send the found value
    int found = close_file_descriptor(token);
    int index_val = get_pid_from_table(tid);
    //Release it from token table
    if(index_val >= 0 && index_val < MAX_PROCESSES) {
      for(int i=0;i<MAX_TOKENS;i++) {
	if(process_table[index_val].token_table[i] == token)
	  process_table[index_val].token_table[i] = -1;
      }
    }
    L4_MsgClear(&msg);
    L4_Set_MsgMsgTag(&msg, L4_Niltag);
    L4_MsgAppendWord(&msg, found);
    L4_MsgLoad(&msg);
    dprintf(2, "Message for SOS_RELEASE_TOKEN assembled.\n");
    return 1;
}

static int sos_rpc_write_perms(void) {
    //dprintf(0, "got sos_rpc_write_perms call\n");
    // Extract actual message
    // first word is the file descriptor token
    int token = L4_MsgWord(&msg, 0);
    int index = get_index_from_token(token);
    L4_Word_t messageWord;
    Token_Descriptor_t* file = &token_table[index];
    //dprintf(0, "writing to token %ld\n", token);
    // find if the file is correctly opened for writing
    if (!has_access(tid, token, FM_WRITE))
    {
        dprintf(0, "file %s is not opened for writing by %ld\n", file->filename, L4_ThreadNo(tid));
        messageWord = SOS_WRITE_INVALID_TOKEN;
    }

    if (strcmp(token_table[index].filename, "console") == 0)
    {
        messageWord = SOS_WRITE_CONSOLE;
    } else {
        messageWord = SOS_WRITE_FILE;
    }
    L4_MsgClear(&msg);
    L4_Set_MsgMsgTag(&msg, L4_Niltag);
    L4_MsgAppendWord(&msg, messageWord);
    L4_MsgLoad(&msg);
    dprintf(2,"Write perms .. token %d has messageword %d",token,messageWord);
    return 1;
    }

static int sos_rpc_read(void)
{
  //dprintf(0, "Read token received\n");
    int token = (int) L4_MsgWord(&msg,0);
    int index = get_index_from_token(token);
    Token_Access_t* access_threads = get_access_from_token(token);
    int bytes = (int) L4_MsgWord(&msg,1);
    //dprintf(0,"Token is %d Index is %d: bytes are %d \n", token, index, bytes);
    char access = has_access(tid, token, FM_READ);
    L4_Word_t messageWord;
    if (!access)
    {
      //dprintf(0, "no access for reading\n");
        messageWord = SOS_READ_INVALID_TOKEN;
    } else if (strcmp(token_table[index].filename, "console") == 0)
    {
        messageWord = SOS_READ_CONSOLE;
    } else {
        messageWord = SOS_READ_FILE;
    }
    L4_MsgClear(&msg);
    L4_Set_MsgMsgTag(&msg, L4_Niltag);
    L4_MsgAppendWord(&msg, messageWord);
    L4_MsgLoad(&msg);
    L4_Reply(tid);
    if(access) {
        if (strcmp(token_table[index].filename, "console") == 0)
        {
            //Now we need to check for the handler
            //Now we need to block and flush buffer
	    //Register it for messages
	    while(BUFFER_LOCK) {
	        ;
	    }
	    BUFFER_LOCK = 1;
	    for(int i=0;i<MAX_CONSOLE_READERS;i++) {
	      if(L4_ThreadNo(console_listeners[i].tid) == L4_ThreadNo(L4_nilthread)) {
		console_listeners[i].tid = tid;
		console_listeners[i].number_bytes_left = bytes;
		console_listeners[i].read_enabled = 1;
	      }
	    }
	    BUFFER_LOCK = 0;
	      //blocking_send_buffered_input(tid,bytes);
        } else {
            // delegate this to nfs_read
            // TODO: position
            struct cookie fh; int size;
            assert(get_file_handle(token_table[index].filename, &fh, &size) == 0);
            int bytes_to_read = (access_threads->read_offset + bytes <= access_threads->max_read_offset) ? 
                bytes : 
                access_threads->max_read_offset - access_threads->read_offset;
            nfs_read(&fh, access_threads->read_offset, bytes_to_read, nfs_read_callback, (uintptr_t) token);
        }
    }

    //dprintf(0, "Sending back found=%d\n", found);
    return 0;
}

static int sos_rpc_stat(void)
{
    // get the filename
    char filename[FILE_NAME_SIZE];
    L4_MsgGet(&msg, (L4_Word_t*)filename);
    nfs_lookup(&mnt_point, filename, nfs_lookup_callback, (uintptr_t) tid.raw);
    return 0;
}

static int sos_rpc_getdirent(void)
{
    // get the position
    int pos = (int) L4_MsgWord(&msg, 0);
    // reset the filename iterator
    filename_iterator = 0;
    //dprintf(0, "getdirent issuing readdir\n");
    nfs_readdir(&mnt_point, 0, FILE_NAME_SIZE, nfs_readdir_callback, (uintptr_t) L4_Myself().raw);
    // go to sleep
    L4_ThreadId_t root_threadId = L4_Pager();
    //dprintf(0, "rpc thread %lx going to sleep\n", L4_Myself().raw);
    L4_Receive(root_threadId);
    //dprintf(0, "rpc thread woken up\n");
    L4_MsgClear(&msg);
    L4_Set_MsgMsgTag(&msg, L4_Niltag);
    if (pos < filename_iterator)
    {
        char* toSend = files[pos].chars;
        L4_Set_MsgLabel(&msg, GETDIRENT_VALID_FILE);
        int count = strlen(toSend) + 1;
        for (int i = 0; i < count;) // i incremented in the loop 
	    {
		    L4_Word_t word = 0;
		    // add bytes one by one
	        char* writeTo = (char*) &word;
	        for (int k = 0; k < sizeof(L4_Word_t) && i < count; k++)
	        {
			    *(writeTo++) = toSend[i++];
		    }
		    L4_MsgAppendWord(&msg, word);
	    }

        //dprintf(0, "reply for getdirent assembled to tid %lx\n", tid.raw);
    } else if (pos == filename_iterator)
    {
      //dprintf(0, "position %d is the first afterwards\n", pos);
        L4_Set_MsgLabel(&msg, GETDIRENT_FIRST_EMPTY_FILE);
    } else {
      //dprintf(0, "position %d is way out of range\n", pos);
        L4_Set_MsgLabel(&msg, GETDIRENT_OUT_OF_RANGE);
    }
    L4_MsgLoad(&msg);

    return 1;
}

static int sos_rpc_process_delete(void) {
    int pid_kill = (int) L4_MsgWord(&msg,0);
    dprintf(0,"Got a process delete %d %lx by %lx",pid_kill,L4_ThreadNo(tid),L4_Myself().raw);
    L4_Msg_t msg1;
    L4_MsgTag_t tag1;
    L4_ThreadId_t tid_kill = L4_nilthread;
    int returnval = -1;
    if(pid_kill >=0 && pid_kill < MAX_PROCESSES 
       && L4_ThreadNo(process_table[pid_kill].tid) != L4_ThreadNo(L4_nilthread)) {
      //Send message all the 
      tid_kill = process_table[pid_kill].tid;
      int killval = sos_delete_task(tid_kill,L4_Pager());
      returnval = 0;
      dprintf(0,"Returnval of process kill is %d\n",killval);
      for(int i=0;i<MAX_TOKENS;i++) {
	if(process_table[pid_kill].token_table[i] != -1) {
	  close_file_descriptor(process_table[pid_kill].token_table[i]);
	  process_table[pid_kill].token_table[i] = -1;
	}
      }
      for(int i=0;i<MAX_WAITING_TID;i++) {
	if(L4_ThreadNo(process_table[pid_kill].waiting_tid[i]) 
	   != L4_ThreadNo(L4_nilthread)) {
	  //Send a message to it so that its process_create or process_wait calls can receive
	      L4_MsgClear(&msg1);
	      L4_Set_MsgMsgTag(&msg1, L4_Niltag);
	      L4_MsgAppendWord(&msg1, pid_kill);
	      L4_MsgLoad(&msg1);
	      tag1 = L4_Reply(process_table[pid_kill].waiting_tid[i]);
	      if(L4_IpcFailed(tag1)) {
		dprintf(0,"Failed to reply to waiting tid %lx",process_table[pid_kill].waiting_tid[i]);
	      }
	      process_table[pid_kill].waiting_tid[i] = L4_nilthread;
	}
      }
      //We do not signal returnval failure here
      for(int i=0;i<MAX_PROCESSES;i++) {
        if(L4_ThreadNo(any_process_list[i]) != L4_ThreadNo(L4_nilthread)) {
       	      L4_MsgClear(&msg1);
	      L4_Set_MsgMsgTag(&msg1, L4_Niltag);
	      L4_MsgAppendWord(&msg1, pid_kill);
	      L4_MsgLoad(&msg1);
	      tag1 = L4_Reply(any_process_list[i]);
	      if(!L4_IpcFailed(tag1)) {
                any_process_list[i] = L4_nilthread;
	      }
        }
      }
	//Sucessful close
      process_table[pid_kill].tid = L4_nilthread;
      process_table[pid_kill].size = 0;
      process_table[pid_kill].stime = 0;
      strcpy(process_table[pid_kill].command,"");
	//Now remove the entries from the pagetable for the process
      L4_MsgClear(&msg1);
      L4_Set_MsgLabel(&msg1,MAKETAG_SYSLAB(SOS_SYSCALL_REMOVE_TID_PAGE));
      L4_MsgAppendWord(&msg1,tid_kill.raw);
      L4_MsgLoad(&msg1);
      L4_Send(L4_Pager());
    }
    //If the process to be killed invoked process_delete on itself we will not reply back
    if(L4_ThreadNo(tid_kill) == L4_ThreadNo(tid)) {
      dprintf(0,"Not replying back to tid %lx",L4_ThreadNo(tid));
      return 0;
    }
    L4_MsgClear(&msg);
    L4_Set_MsgMsgTag(&msg, L4_Niltag);
    L4_MsgAppendWord(&msg, returnval);
    L4_MsgLoad(&msg);
    dprintf(0,"Replying to tid %lx",L4_ThreadNo(tid));
    return 1;
}

static int sos_rpc_process_id(void) {
    dprintf(0,"Got token process_id from tid %lx\n",tid);
    int i = get_pid_from_table(tid);
    if(i >= MAX_PROCESSES) {
        //It was not found in the process table
        i = -1;
    }
    L4_MsgClear(&msg);
    L4_Set_MsgMsgTag(&msg, L4_Niltag);
    L4_MsgAppendWord(&msg, i);
    L4_MsgLoad(&msg);
    return 1;
}

static int sos_rpc_process_wait(void) {
  int pid = (int) L4_MsgWord(&msg,0);
  dprintf(0,"Got token process_wait for pid %d",pid);
  int errorFlag = 1;
  int returnval = -1;
  if(pid == -1) {
    //Need to add it in the any list
    for(int i=0;i<MAX_PROCESSES;i++) {
      if(L4_ThreadNo(any_process_list[i]) == L4_ThreadNo(L4_nilthread)) {
        any_process_list[i] = tid;
        errorFlag = 0;
        break;
      }
    }
  } else {
    if(pid >= 0 && pid < MAX_PROCESSES && L4_ThreadNo(process_table[pid].tid) != L4_ThreadNo(L4_nilthread)) {
      for(int i=0;i<MAX_WAITING_TID;i++) {
        if(L4_ThreadNo(process_table[pid].waiting_tid[i]) == L4_ThreadNo(L4_nilthread)) {
          process_table[pid].waiting_tid[i] = tid;
          errorFlag = 0;
          break;
        }
      }
    }
  }
  if(errorFlag) {
    L4_MsgClear(&msg);
    L4_Set_MsgMsgTag(&msg, L4_Niltag);
    L4_MsgAppendWord(&msg, returnval);
    L4_MsgLoad(&msg);
    return 1;
  }
  dprintf(0,"Not returning a reply");
  return 0;
}


static int sos_rpc_process_stat(void) {
    int max_processes = L4_MsgWord(&msg,0);
    int returnval = 0;
    for(int i=0,count=1;i<MAX_PROCESSES && count <= max_processes;i++) {

      if(L4_ThreadNo(process_table[i].tid) != L4_ThreadNo(L4_nilthread)) {
	//We need to send the message
	//L4_KDB_Enter("hell");
	count++;
	returnval = send_in_one_message(tid,SEND_STAT_COMMAND,process_table[i].command,strlen(process_table[i].command));
	tag = L4_Receive(tid);
	if(L4_IpcFailed(tag)) {
	  return 0;
	}
	L4_MsgClear(&msg);
	L4_Set_MsgLabel(&msg,MAKETAG_SYSLAB(SEND_STAT_DATA));
	//Pid value
	L4_MsgAppendWord(&msg, i);
	L4_MsgAppendWord(&msg, process_table[i].size);
	L4_MsgAppendWord(&msg, process_table[i].stime/1000);
	L4_MsgAppendWord(&msg, 0);
	L4_MsgLoad(&msg);
	tag = L4_Call(tid);
	if(L4_IpcFailed(tag)) {
	  return 0;
	}
      }
    }
    //Now we need to send an end message
    L4_MsgClear(&msg);
    L4_Set_MsgLabel(&msg,MAKETAG_SYSLAB(SEND_STAT_END));
    L4_MsgLoad(&msg);
    //L4_KDB_Enter("q");
    return 1;
}

void process_table_add_creation_entry(int index,L4_ThreadId_t newtid,unsigned success) {
  if(success) {
    process_table[index].tid = newtid;
    process_table[index].stime = time_stamp();
  } else {
    //Unsuccessful process create, free it up
    process_table[index].tid = L4_nilthread;
    strcpy(process_table[index].command,"");
  }
}

static int sos_rpc_process_create(void) {
    char execname[FILE_NAME_SIZE];
    L4_MsgGet(&msg, (L4_Word_t *)execname);
    //0 terminate it
    execname[FILE_NAME_SIZE-1]=0;
    dprintf(0,"Got process create %s\n",execname);
    int pid = -1;
    int index = -1;
    index = get_empty_pid();
    //We want to load something from the file system here
    if(index >= MAX_PROCESSES) {
          L4_MsgClear(&msg);
	  L4_MsgAppendWord(&msg, pid);
	  L4_MsgLoad(&msg);
	  return 1;
    }
    //This will prevent this process table entry from being manipulated until the read callbacks are finished
    process_table[index].tid = L4_Myself();
    process_table[index].size = 0;
    for(int j=0;j<MAX_TOKENS;j++) {
      process_table[index].token_table[j] = -1;
    }
    strcpy(process_table[index].command,execname); 
    for(int k=0;k<MAX_WAITING_TID;k++) {
      process_table[index].waiting_tid[k] = L4_nilthread;
    }
    struct token_process_t *token_val = (struct token_process_t *) malloc(sizeof(struct token_process_t));
    token_val -> creating_process_tid = tid;
    dprintf(0,"%lx %lx %p\n",tid.raw,token_val->creating_process_tid.raw,token_val);
    token_val -> process_table_index = index;
    //The callback will reply then
    nfs_lookup(&mnt_point,execname,process_create_lookup_callback,(uintptr_t) token_val);
    return 0;
}



/* This is similar to syscall loop but we want
to run it as a separate thread in order to handle
blocking calls */
void rpc_thread(void)
{
    dprintf(2, "Entering rpc_loop");
    L4_Accept(L4_UntypedWordsAcceptor);
    
    if(L4_ThreadNo(rpc_threadId) == L4_ThreadNo(L4_nilthread))
      rpc_threadId = L4_Myself();

    tid = L4_nilthread;
    
    // initialize token table
    for (int i = 0; i < MAX_TOKENS; i++)
    {
        token_table[i].read_count = 0;
        token_table[i].write_count = 0;
        token_table[i].access_count = 0;
    }
    
    int send = 0;

    //Initialise read listener buffer
    for(int i=0;i<MAX_CONSOLE_READERS;i++) {
      console_listeners[i].tid = L4_nilthread;
      console_listeners[i].number_bytes_left = 0;
      console_listeners[i].read_enabled = 0;
    }

    for(int i=0;i<MAX_PROCESSES;i++) {
      process_table[i].tid = L4_nilthread;
      process_table[i].size = 0;
      any_process_list[i] = L4_nilthread;
      strcpy(process_table[i].command,"");
    }
    
    for(;;) {
    if (!send)
        tag = L4_Wait(&tid); 
    else {
        tag = L4_ReplyWait(tid, &tid); 
    }

    if (L4_IpcFailed(tag)) {
        L4_Word_t ec = L4_ErrorCode();
        dprintf(0, "%s: IPC error\n", __FUNCTION__);
        sos_print_error(ec);
        assert( !(ec & 1) );    // Check for recieve error and bail
        send = 0;
        continue;
    }

    // At this point we have, probably, recieved an IPC
    L4_MsgStore(tag, &msg); /* Get the tag */

    dprintf(2, "%s: got msg from %lx, (%d %p)\n", __FUNCTION__,
         L4_ThreadNo(tid), (int) TAG_SYSLAB(tag),
         (void *) L4_MsgWord(&msg, 0));
    

    send = 1; /* In most cases we will want to send a reply */
    switch (TAG_SYSLAB(tag)) {
        case SOS_RPC_GET_TOKEN:
            send = sos_rpc_get_token();
            break;
        case SOS_RPC_WRITE:
            send = sos_rpc_write();
            break;
        case SOS_RPC_RELEASE_TOKEN:
            send = sos_rpc_release_token();
            break;
        case SOS_RPC_READ :
            send = sos_rpc_read();
            break;
        case SOS_RPC_STAT:
            send = sos_rpc_stat();
            break;
        case SOS_RPC_GETDIRENT:
            send = sos_rpc_getdirent();
            break;
        case SOS_RPC_WRITE_PERMS:
          send = sos_rpc_write_perms();
          break;
        case SOS_RPC_PROCESS_CREATE:
            send = sos_rpc_process_create();
            break;
        case SOS_RPC_PROCESS_ID:
	    send = sos_rpc_process_id();
	    break;
        case SOS_RPC_PROCESS_DELETE:
	    send = sos_rpc_process_delete();
	    break;
        case SOS_RPC_PROCESS_WAIT:
            send = sos_rpc_process_wait();
            break;
        case SOS_RPC_PROCESS_STAT:
	    send = sos_rpc_process_stat();
	    break;
	case SOS_RPC_TIMESTAMP:
          //dprintf(0, "timestamp message called\n");
	    L4_Set_MsgMsgTag(&msg, L4_Niltag);
	    timestamp_t timeval = time_stamp();
	    //dprintf(0,"Time stamp obtained %llu",timeval);
  	    L4_MsgAppendWord(&msg, timeval/TIME_SPLIT);
	    L4_MsgAppendWord(&msg, timeval%TIME_SPLIT);
	    L4_MsgLoad(&msg);
	    break;
	case SOS_RPC_SLEEP:
            dprintf(0, "sleep message called\n");
	    int delay = (int) L4_MsgWord(&msg,0);
	    L4_Set_MsgMsgTag(&msg, L4_Niltag);
	    dprintf(0,"Delay is %d\n",delay);
	    int returnval = register_timer(delay*1000,tid);
  	    L4_MsgAppendWord(&msg, returnval);
	    L4_MsgLoad(&msg);
	    break;
       default:
            // Unknown system call, so we don't want to reply to this thread
            sos_print_l4memory(&msg, L4_UntypedWords(tag) * sizeof(uint32_t));
            send = 0;
            break;
    }

    }
}

