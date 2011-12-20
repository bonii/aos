#include <sos.h>
#include <ttyout.h>
#include <l4/ipc.h>
#include <l4/message.h>
#include <string.h>
#include <assert.h>
#include "../../../helpers/ipc_defs.h"

// to be implemented!

fildes_t stdout_fd = 0;

static L4_ThreadId_t rpc_threadId;
static L4_ThreadId_t root_threadId;

static void sos_init(void)
{
    L4_Msg_t msg;
    L4_MsgClear(&msg);
    L4_Set_MsgLabel(&msg,MAKETAG_SYSLAB(SOS_SYSCALL_FIND_RPC_THREAD));
    L4_MsgLoad(&msg);
    L4_MsgTag_t tag;
    root_threadId = L4_Pager();
    L4_Accept(L4_UntypedWordsAcceptor);
    tag = L4_Call(root_threadId);
    L4_MsgStore(tag, &msg); 
    rpc_threadId.raw = L4_MsgWord(&msg, 0);

}


static int send_in_one_message(L4_ThreadId_t receiver, L4_Word_t MsgLabel, const char* data, int count)
{
   	L4_Msg_t msg;
	L4_MsgClear(&msg);
	L4_Set_MsgLabel(&msg, MAKETAG_SYSLAB(MsgLabel));
	for (int i = 0; i < count;) // i incremented in the loop 
	{
		L4_Word_t word = 0;
		// add bytes one by one
	    char* writeTo = (char*) &word;
	    for (int k = 0; k < sizeof(L4_Word_t) && i < count; k++)
	    {
			*(writeTo++) = data[i++];
		}
		L4_MsgAppendWord(&msg, word);

	}
	L4_MsgLoad(&msg);
	L4_MsgTag_t tag = L4_Send(receiver);
	if (L4_IpcFailed(tag))
	{
	    return -1;
	}
	return 0;
}

fildes_t open(const char *path, fmode_t mode)
{ 
  /* Generate a message for the kernel for the token */
  long start = time_stamp();
   while(L4_ThreadNo(rpc_threadId) == L4_ThreadNo(L4_nilthread)) {
        sos_init();
   }
    char file_name_mode[FILE_NAME_MODE_SIZE];
    for (int i = 0; i < FILE_NAME_MODE_SIZE; i++)
    {
        file_name_mode[i] = 0;
    }
    if (strlen(path) > FILE_NAME_SIZE)
    {
        return -1;
    }
    
    strcpy(file_name_mode, path);
    file_name_mode[FILE_NAME_SIZE] = mode;
    
    int result = send_in_one_message(rpc_threadId, SOS_RPC_GET_TOKEN, file_name_mode, FILE_NAME_MODE_SIZE);
    if (result < 0) return result;
    L4_MsgTag_t tag;
    L4_Msg_t msg;
    L4_Accept(L4_UntypedWordsAcceptor);
    tag = L4_Receive(rpc_threadId);
    L4_MsgStore(tag, &msg);
    //printf("words: %d\n", tag.X.u);
    assert(tag.X.u == 1);
    fildes_t token = (fildes_t) L4_MsgWord(&msg, 0);
    //printf("got token %d\n", token);
    long end = time_stamp();
    if (0) printf("Time for open: %ld\n",(end-start));
    return token;
}

int close(fildes_t file)
{
    int success = 0;
    while(L4_ThreadNo(rpc_threadId) == L4_ThreadNo(L4_nilthread)) {
        sos_init();
    }
    L4_Msg_t msg;
    L4_MsgClear(&msg);
    L4_Set_MsgLabel(&msg,MAKETAG_SYSLAB(SOS_RPC_RELEASE_TOKEN));
    L4_MsgAppendWord(&msg,(L4_Word_t) file);
    L4_MsgLoad(&msg);

    L4_MsgTag_t tag;
    L4_Accept(L4_UntypedWordsAcceptor);
    tag = L4_Call(rpc_threadId);
    L4_MsgStore(tag, &msg); 
    success = (int) L4_MsgWord(&msg, 0);

    return success;
}

int read(fildes_t file, char *buf, size_t nbyte)
{
    while(L4_ThreadNo(rpc_threadId) == L4_ThreadNo(L4_nilthread)) {
        sos_init();
    }
    long start = time_stamp();
    L4_Msg_t msg;
    L4_MsgTag_t tag;
    
    L4_MsgClear(&msg);
    L4_Set_MsgLabel(&msg,MAKETAG_SYSLAB(SOS_RPC_READ));
    L4_MsgAppendWord(&msg,(L4_Word_t) file);
    L4_MsgAppendWord(&msg,(L4_Word_t) nbyte);
    L4_MsgLoad(&msg);

    L4_Accept(L4_UntypedWordsAcceptor);
    //printf("Here then %lx %lx\n",L4_ThreadNo(L4_Myself()),L4_ThreadNo(rpc_threadId));
    tag = L4_Call(rpc_threadId);
    //printf("Here now\n");
    L4_MsgStore(tag, &msg);
    L4_Word_t result = L4_MsgWord(&msg, 0);
    //printf("Opened file with return code %d\n", (int)tmp);
    if (result == SOS_READ_INVALID_TOKEN) return -1;
    
    int i;
    for (i = 0; i < nbyte; i++)
    {
      //printf("waiting for byte number %d\n", i);
      //L4_Accept(L4_UntypedWordsAcceptor);
        tag = L4_Receive(root_threadId);
	//printf("Baz");
        L4_MsgStore(tag, &msg);
	//printf("Received character %c\n",buf[i]);
        if (L4_Label(tag) > 0 ) break;
        assert(tag.X.u == 1);
        L4_Word_t word = L4_MsgWord(&msg, 0);
        buf[i] = ((char*) &word)[0];

        //buf[i] = (char) word;
        //printf("the word %lx characters are '%c' '%c' '%c' '%c' \n", tmp, ((char*) &tmp)[0], ((char*) &tmp)[1], ((char*) &tmp)[2], ((char*) &tmp)[3]);
        //printf("got character '%c', word: %lx\n", buf[i], word);
        if (buf[i] == '\n')
        {
            i++;
            break;
        }
    }
    //printf("i: %d buffer: %s\n", i, buf);
    //printf("Am here");
    long end = time_stamp();
    if (0 && (result != SOS_READ_CONSOLE)) printf("Time for reading %d bytes is %ld\n",i,(end-start));
    return i;
}

int write(fildes_t file, const char *buf, size_t nbyte)
{
    while(L4_ThreadNo(rpc_threadId) == L4_ThreadNo(L4_nilthread)) {
        sos_init();
    }
    long start = time_stamp();
    L4_Msg_t msg;
    L4_MsgTag_t tag;
    L4_MsgClear(&msg);
    L4_Set_MsgLabel(&msg,MAKETAG_SYSLAB(SOS_RPC_WRITE_PERMS));
    L4_MsgAppendWord(&msg,(L4_Word_t) file);
    L4_MsgLoad(&msg);

    L4_Accept(L4_UntypedWordsAcceptor);
    //printf("Here then %lx %lx\n",L4_ThreadNo(L4_Myself()),L4_ThreadNo(rpc_threadId));
    tag = L4_Call(rpc_threadId);
    //printf("Here now\n");
    L4_MsgStore(tag, &msg);
    L4_Word_t result = L4_MsgWord(&msg, 0);
    //printf("Opened file with return code %d\n", (int)tmp);
    if (result == SOS_WRITE_INVALID_TOKEN) return -1;
    int sent = 0;
    if(result == SOS_WRITE_CONSOLE) {
        sent = send_message_with_info((L4_Word_t) file, rpc_threadId, SOS_RPC_WRITE, buf, nbyte,L4_nilthread);
    } else {
        sent = send_message_with_info((L4_Word_t) file, rpc_threadId, SOS_RPC_WRITE, buf, nbyte,root_threadId);
    }
    long end = time_stamp();
    if(0) printf("Time take to write %d bytes is %ld",sent,(end-start));
    return sent;
}

int getdirent(int pos, char *name, size_t nbyte)
{
    while(L4_ThreadNo(rpc_threadId) == L4_ThreadNo(L4_nilthread)) {
        sos_init();
    }
    // set to 0
    for (int i = 0; i < nbyte; i++)
        name[i] = 0;
    // send a message to get the directory entry
    L4_Msg_t msg;
    L4_MsgClear(&msg);
    L4_Set_MsgLabel(&msg,MAKETAG_SYSLAB(SOS_RPC_GETDIRENT));
    L4_MsgAppendWord(&msg,(L4_Word_t) pos);
    L4_MsgLoad(&msg);
    
    L4_MsgTag_t tag;
    L4_Accept(L4_UntypedWordsAcceptor);
    tag = L4_Call(rpc_threadId);
    if (L4_IpcFailed(tag)) {
        printf("error in getdirent IPC\n");
        return -1;
    }
    L4_MsgStore(tag, &msg);
    int result = L4_Label(tag);
    if (result == GETDIRENT_VALID_FILE)
    {
        int file_name_size = tag.X.u*sizeof(L4_Word_t);
        assert(file_name_size > 0);
        char buf[file_name_size];
        for (int i = 0; i < file_name_size; i++)
            buf[i] = 0;

        L4_MsgGet(&msg, (L4_Word_t*)buf);

        file_name_size = strlen(buf);
        //printf("seems to be a valid file. filename size: %d, words: %d, word0: %lx string: %s\n", file_name_size, tag.X.u, L4_MsgWord(&msg, 0), buf);

        int i;
        for (i = 0; i < nbyte && i < file_name_size; i++)
        {
            name[i] = buf[i];
        }
        //printf("getdirent returning with message '%s'\n", name);
        return i;
    } else if (result == GETDIRENT_FIRST_EMPTY_FILE)
    {
        //printf("is the first file afterwards.\n");
        return 0;
    } else { // GETDIRENT_OUT_OF_RANGE
        //printf("is way out of range.\n");
        return -1;
    }
}

int stat(const char *path, stat_t *buf)
{
    while(L4_ThreadNo(rpc_threadId) == L4_ThreadNo(L4_nilthread)) {
        sos_init();
    }
    send_in_one_message(rpc_threadId, SOS_RPC_STAT, path, strlen(path)+1);
    L4_Msg_t msg;
    L4_MsgTag_t tag = L4_Receive(root_threadId);
    L4_MsgStore(tag, &msg);
    if (tag.X.u < 5)
    {
        return -1;
    }
    buf->st_type = (st_type_t) L4_MsgWord(&msg, 0);
    buf->st_fmode = (fmode_t) L4_MsgWord(&msg, 1);
    buf->st_size = (size_t) L4_MsgWord(&msg, 2);
    buf->st_ctime = (long) L4_MsgWord(&msg, 3);
    buf->st_atime = (long) L4_MsgWord(&msg, 4);
    return 0;
}

pid_t process_create(const char *path)
{
    while(L4_ThreadNo(rpc_threadId) == L4_ThreadNo(L4_nilthread)) {
        sos_init();
    }
    L4_Msg_t msg;
    L4_MsgTag_t tag;
    pid_t process_id = -1;
    int result = send_in_one_message(rpc_threadId, SOS_RPC_PROCESS_CREATE, path, strlen(path));
    if(result != -1) {
        //The message was successfully sent, receive the process id from the rpc thread
        tag = L4_Receive(rpc_threadId);
        L4_MsgStore(tag,&msg);
        process_id = (pid_t)L4_MsgWord(&msg,0);
    }
    return process_id;
}

int process_delete(pid_t pid)
{
    while(L4_ThreadNo(rpc_threadId) == L4_ThreadNo(L4_nilthread)) {
        sos_init();
    }
    L4_Msg_t msg;
    L4_MsgTag_t tag;
    int returnval = -1;
    L4_MsgClear(&msg);
    L4_Set_MsgLabel(&msg,MAKETAG_SYSLAB(SOS_RPC_PROCESS_DELETE));
    L4_MsgAppendWord(&msg,(L4_Word_t) pid);
    L4_MsgLoad(&msg);
    tag = L4_Call(rpc_threadId);
    if(!L4_IpcFailed(tag)) {
      L4_MsgStore(tag,&msg);
      returnval = (int) L4_MsgWord(&msg,0);
    }
    return returnval;
}

pid_t my_id(void)
{
    while(L4_ThreadNo(rpc_threadId) == L4_ThreadNo(L4_nilthread)) {
        sos_init();
    }
    L4_Msg_t msg;
    L4_MsgTag_t tag;
    int returnval = -1;
    L4_MsgClear(&msg);
    L4_Set_MsgLabel(&msg,MAKETAG_SYSLAB(SOS_RPC_PROCESS_ID));
    L4_MsgLoad(&msg);
    tag = L4_Call(rpc_threadId);
    L4_MsgStore(tag,&msg);
    returnval = (pid_t) L4_MsgWord(&msg,0);
    return returnval;
}

int process_status(process_t *processes, unsigned max)
{
    while(L4_ThreadNo(rpc_threadId) == L4_ThreadNo(L4_nilthread)) {
       sos_init();
    }
    L4_Msg_t msg;
    L4_MsgTag_t tag;
    L4_MsgClear(&msg);
    L4_Set_MsgLabel(&msg,MAKETAG_SYSLAB(SOS_RPC_PROCESS_STAT));
    L4_MsgAppendWord(&msg,(L4_Word_t) max);
    L4_MsgLoad(&msg);
    L4_Send(rpc_threadId);
    int counter = 0, breakloop = 0;
    char command_val[N_NAME];
    while(1) {
        if (breakloop || counter == max) 
	    break;
	tag = L4_Receive(rpc_threadId);
	L4_MsgStore(tag,&msg);
        switch (TAG_SYSLAB(tag)) {
	  case SEND_STAT_COMMAND :
	    L4_MsgGet(&msg,(L4_Word_t *) command_val);
	    strcpy(processes[counter].command,command_val);
	    processes[counter].command[strlen(command_val)];
	    L4_Reply(rpc_threadId);
	    break;
	  case SEND_STAT_DATA :
	    processes[counter].pid = L4_MsgWord(&msg,0);
	    processes[counter].size = (int) L4_MsgWord(&msg,1);
	    processes[counter].stime =  L4_MsgWord(&msg,2);
	    processes[counter].ctime =  L4_MsgWord(&msg,3);;
	    counter++;
	    L4_Reply(rpc_threadId);
	    break;
	  case SEND_STAT_END :
	    breakloop = 1;
	}
    }
  return counter;
}

pid_t process_wait(pid_t pid)
{
    while(L4_ThreadNo(rpc_threadId) == L4_ThreadNo(L4_nilthread)) {
        sos_init();
    }
    L4_Msg_t msg;
    L4_MsgTag_t tag;
    int returnval = -1;
    L4_MsgClear(&msg);
    L4_Set_MsgLabel(&msg,MAKETAG_SYSLAB(SOS_RPC_PROCESS_WAIT));
    L4_MsgAppendWord(&msg,(L4_Word_t) pid);
    L4_MsgLoad(&msg);
    tag = L4_Call(rpc_threadId);
    if(!L4_IpcFailed(tag)) {
      L4_MsgStore(tag,&msg);
      returnval = (pid_t) L4_MsgWord(&msg,0);
    }
    return returnval;
}

long time_stamp(void)
{
    while(L4_ThreadNo(rpc_threadId) == L4_ThreadNo(L4_nilthread)) {
        sos_init();
    }
    L4_Msg_t msg;
    L4_MsgClear(&msg);
    L4_Set_MsgLabel(&msg,MAKETAG_SYSLAB(SOS_RPC_TIMESTAMP));
    L4_MsgLoad(&msg);
    L4_MsgTag_t tag;
    L4_Accept(L4_UntypedWordsAcceptor);
    tag = L4_Call(rpc_threadId);
    if (L4_IpcFailed(tag)) {
      //printf("error in gettimestamp IPC\n");
        return -1;
    }
    L4_MsgStore(tag, &msg);
    long timeval = (uint64_t) L4_MsgWord(&msg,0)*TIME_SPLIT + (uint64_t) L4_MsgWord(&msg,1);
    //printf("Time is %llu \n",timeval);
    return timeval;
}

//Need a kernel to sleep until block
void sleep(int msec)
{
    // TODO: blocks forever for now
    L4_Msg_t msg;
    L4_MsgClear(&msg);
    L4_Set_MsgLabel(&msg,MAKETAG_SYSLAB(SOS_RPC_SLEEP));
    L4_MsgAppendWord(&msg,(L4_Word_t) msec);
    L4_MsgLoad(&msg);
    L4_MsgTag_t tag;
    L4_Accept(L4_UntypedWordsAcceptor);
    tag = L4_Call(rpc_threadId);
    L4_MsgStore(tag,&msg);
    int sleep_registered = (int) L4_MsgWord(&msg,0);
    if(!sleep_registered) {
      //Successfully registered we need to wait
      L4_Receive(root_threadId);
    }
}

int share_vm(void *adr, size_t size, int writable)
{
    return 0;
}
