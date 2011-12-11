#include "sos_rpc.h"

#include <stdio.h>
#include <assert.h>

#include "libsos.h"

#include <serial.h>
#include "sos_rpc_callbacks.h"

struct serial * serial_port = 0;

static L4_ThreadId_t rpc_threadId;
static Token_Descriptor_t token_table[MAX_TOKENS];
static L4_Msg_t msg;
static L4_MsgTag_t tag;
static L4_ThreadId_t tid;
static read_listener_t console_listeners[MAX_CONSOLE_READERS];

static int BUFFER_LOCK;

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
  //dprintf(0, "got input token %c\n", input);
    char temp[2];
    int i = 0;
    temp[0] = input;
    temp[1] = 0;
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
    int buffer_index_flushed = 0;
    for(i=0;i<strlen(read_buffer);i++) {
      //dprintf(0,"%c .. buffer",read_buffer[i]);
      for(int j=0;j<MAX_CONSOLE_READERS;j++) {
	if(L4_ThreadNo(console_listeners[j].tid) != L4_ThreadNo(L4_nilthread)
	   && console_listeners[j].number_bytes_left > 0 
	   && console_listeners[j].read_enabled) {

	  //L4_MsgTag_t tag;
	        L4_Msg_t msg1;
		L4_MsgClear(&msg1);
		L4_Set_MsgMsgTag(&msg1, L4_Niltag);
		L4_Set_MsgLabel(&msg1, 0);
		L4_Word_t word = 0;
		char* writeTo = (char*) &word;
		writeTo[0] = read_buffer[i];
		L4_MsgAppendWord(&msg1, word);
		L4_MsgLoad(&msg1);
		dprintf(0,"Sending message to %lx %c\n",L4_ThreadNo(console_listeners[j].tid)
			,read_buffer[i]);
		L4_Send(console_listeners[j].tid);
		dprintf(0,"After message sent \n:");
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
    for(i=buffer_index_flushed;i<BUFFER_SIZE;i++,counter++) {
      read_buffer[counter] = read_buffer[buffer_index_flushed+counter+1];
      
    }
    read_buffer[counter]=0;
    dprintf(0,"read buffer is %s\n",read_buffer);
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

    /*dprintf(0, "cookie in get_file_handle: ");
    for (int i = 0 ; i < FHSIZE; i++)
    {
        dprintf(0, "%c", writeTo->data[i]);
    }*/
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
            /*        typedef struct sattr 
            {
                unsigned int mode;
                unsigned int uid;
                unsigned int gid;
                unsigned int size;
                timeval_t    atime;
                timeval_t    mtime;
            } sattr_t;*/
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
    //dprintf(0, "got sos_rpc_write call\n");
    // Extract actual message
    //int num_words = tag.X.u - 1;
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
            /*L4_Msg_t new_msg;
            L4_MsgTag_t new_tag = L4_Receive(L4_Pager());
            int status = L4_Label(new_tag);
            dprintf(0, "rpc thread woken up from nfs_write_callback, status is %d\n", status);
            L4_MsgStore(new_tag, &new_msg);
            int size = L4_MsgWord(&new_msg,0);
            bytes_sent = size - access->write_offset;
            access->write_offset = size;*/
            //token_table[token_index].write_file_offset += bytes_sent;
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

static int sos_rpc_release_token(void)
{
    dprintf(2,"Release token received");
    int token = (int) L4_MsgWord(&msg,0);
    //Get the token number from the message which is my index into the datastructure
    int index = get_index_from_token(token);
    int found = -1;
    int access_index = get_access_index_from_token(token);
    Token_Access_t* access = get_access_from_token(token);
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

    //Construct the message and send the found value
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
        //char* toSend = "hellothisisalongnametestfile";
        //dprintf(0, "position %d is a valid file: '%s'\n", pos, toSend);
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
    //    L4_Word_t token = 0;
    //    int found = -1;
    //int num_words; size_t bytes_sent;
    //fmode_t mode = 0;
    //int read_encountered = 0;
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
        default:
            // Unknown system call, so we don't want to reply to this thread
            sos_print_l4memory(&msg, L4_UntypedWords(tag) * sizeof(uint32_t));
            send = 0;
            break;
    }

    }
}