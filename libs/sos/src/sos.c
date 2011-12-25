#include <sos.h>
#include <ttyout.h>
#include <l4/ipc.h>
#include <l4/message.h>
#include <string.h>
#include <assert.h>
#include "../../../helpers/ipc_defs.h"

fildes_t stdout_fd = 0;

static L4_ThreadId_t rpc_threadId;
static L4_ThreadId_t root_threadId;

/*
 * Boot strapping method. Initialise the rpc and root thread ids to be used
 * by other function calls
 */
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

/*
 * Function to send data large enough for a message in one message by packing it in the message
 * registers. No sanity check is done to see if the data fits in one message
 */
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

/*
 * Client side implementation of the open system call defined in the header
 */
fildes_t open(const char *path, fmode_t mode)
{ 
  /* Generate a message for the kernel for the token */
   while(L4_ThreadNo(rpc_threadId) == L4_ThreadNo(L4_nilthread)) {
        sos_init();
   }
   //Pack file name and mode in a char buffer
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
    //Send the buffer(buffer size has been set to be within one message size)
    int result = send_in_one_message(rpc_threadId, SOS_RPC_GET_TOKEN, file_name_mode, FILE_NAME_MODE_SIZE);
    //Failure if message could not be sent
    if (result < 0) return result;
    L4_MsgTag_t tag;
    L4_Msg_t msg;
    L4_Accept(L4_UntypedWordsAcceptor);
    tag = L4_Receive(rpc_threadId);
    L4_MsgStore(tag, &msg);
    assert(tag.X.u == 1);
    fildes_t token = (fildes_t) L4_MsgWord(&msg, 0);
    return token;
}

/*
 * Client side implementation of file close
 */
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

/*
 * Client side implementation of read system call. It blocks until the number of bytes
 * to be read are received or a newline character is encountered
 */
int read(fildes_t file, char *buf, size_t nbyte)
{
    while(L4_ThreadNo(rpc_threadId) == L4_ThreadNo(L4_nilthread)) {
        sos_init();
    }

    L4_Msg_t msg;
    L4_MsgTag_t tag;
    
    L4_MsgClear(&msg);
    L4_Set_MsgLabel(&msg,MAKETAG_SYSLAB(SOS_RPC_READ));
    L4_MsgAppendWord(&msg,(L4_Word_t) file);
    L4_MsgAppendWord(&msg,(L4_Word_t) nbyte);
    L4_MsgLoad(&msg);

    L4_Accept(L4_UntypedWordsAcceptor);
    //Send message to rpc thread and wait to check if read access is allowed
    tag = L4_Call(rpc_threadId);

    L4_MsgStore(tag, &msg);
    L4_Word_t result = L4_MsgWord(&msg, 0);

    //Bail if read access is not allowed
    if (result == SOS_READ_INVALID_TOKEN) return -1;
    
    //Read access is allowed so wait for each byte
    int i;
    for (i = 0; i < nbyte; i++)
    {

        tag = L4_Receive(root_threadId);

        L4_MsgStore(tag, &msg);

	//If there is a tag received then end of file read
        if (L4_Label(tag) > 0 ) break;
        assert(tag.X.u == 1);
        L4_Word_t word = L4_MsgWord(&msg, 0);
        buf[i] = ((char*) &word)[0];

	//If a newline character is encountered then end of console read
        if (buf[i] == '\n')
        {
            i++;
            break;
        }
    }
    return i;
}

/*
 * Client side implementation of the write system call
 */
int write(fildes_t file, const char *buf, size_t nbyte)
{
    while(L4_ThreadNo(rpc_threadId) == L4_ThreadNo(L4_nilthread)) {
        sos_init();
    }
    L4_Msg_t msg;
    L4_MsgTag_t tag;
    L4_MsgClear(&msg);
    L4_Set_MsgLabel(&msg,MAKETAG_SYSLAB(SOS_RPC_WRITE_PERMS));
    L4_MsgAppendWord(&msg,(L4_Word_t) file);
    L4_MsgLoad(&msg);

    L4_Accept(L4_UntypedWordsAcceptor);
    //Check permission for write
    tag = L4_Call(rpc_threadId);

    L4_MsgStore(tag, &msg);
    L4_Word_t result = L4_MsgWord(&msg, 0);

    //Bail out if there are no write perms
    if (result == SOS_WRITE_INVALID_TOKEN) return -1;
    int sent = 0;
    if(result == SOS_WRITE_CONSOLE) {
        sent = send_message_with_info((L4_Word_t) file, rpc_threadId, SOS_RPC_WRITE, buf, nbyte,L4_nilthread);
    } else {
        sent = send_message_with_info((L4_Word_t) file, rpc_threadId, SOS_RPC_WRITE, buf, nbyte,root_threadId);
    }
    return sent;
}

/*
 * Client side implementation of the get directory entries which finds the files in a specific
 * directory (We have a flat directory hierarchy)
 */
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
        int i;
        for (i = 0; i < nbyte && i < file_name_size; i++)
        {
            name[i] = buf[i];
        }
        return i;
    } else if (result == GETDIRENT_FIRST_EMPTY_FILE)
    {
        return 0;
    } else { 
        return -1;
    }
}

/*
 * Client side implementation of the file stat which returns the file information
 */
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

/*
 * Client side implementation of process create
 */
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
        tag = L4_Receive(root_threadId);
        L4_MsgStore(tag,&msg);
        process_id = (pid_t)L4_MsgWord(&msg,0);
    }
    return process_id;
}

/*
 * Client side implementation of process delete 
 */
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

/*
 * Client side implementation of my_id to find the process id of a thread
 */
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

/*
 * Client side implementation of the process status system call which lists the processes 
 * created and their status values. Ctime is not implemented
 */
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
    //Each message received corresponds to a single process data and the client
    //replies back after each process entry to make it synchronous
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

/*
 * Client side implementation of the process wait system where the client blocks until the
 * process it is waiting for exits or any process exits for pid -1
 */
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

/*
 * Client side implementation of the time stamp system call
 */
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
        return -1;
    }
    L4_MsgStore(tag, &msg);
    long timeval = (uint64_t) L4_MsgWord(&msg,0)*TIME_SPLIT + (uint64_t) L4_MsgWord(&msg,1);
    return timeval;
}

/*
 * Client side of the sleep system call. The thread sleeps until the sleep time expires if
 * sleep registration was successful
 */
void sleep(int msec)
{
    //Send a message to be registered in the sleep queue
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
    //If sleep registration was successful wait to be woken up by the root thread
    if(!sleep_registered) {
      //Successfully registered we need to wait
      L4_Receive(root_threadId);
    }
}

int share_vm(void *adr, size_t size, int writable)
{
    return 0;
}
