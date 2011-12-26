#ifndef SOSRPC_H
#define SOSRPC_H

#include "../helpers/ipc_defs.h"
#include "l4.h"
#include "libsos.h"

#define MAX_TOKENS 10
#define MAX_ACCESSES 5
#define BUFFER_SIZE 50
#define MAX_CONSOLE_READERS 1
#define MAX_WAITING_TID 5
#define MAX_PROCESSES 1024
#define MAX_EXEC_NAME 32
#define MAX_EXECUTABLES_IN_IMAGE 20
#define NFS_WRITE_SIZE 256
#define NFS_READ_SIZE 256

void rpc_thread(void);

/*
  Datastructure for token entry for different threads
*/
typedef struct {
  L4_ThreadId_t tid; //tid of the thread holding the token
  fmode_t mode; // mode of access to the file
  int read_offset; //offset for read 
  int write_offset; //offset for write
  //offset to determine the maximum point of read for mutable files
  int max_read_offset; 
} Token_Access_t;

/*
 * Datastructure for token table
*/
typedef struct {
  char filename[FILE_NAME_SIZE]; //file name
  L4_Word_t token; //token value 
  uint8_t read_count, write_count; //No of threads holding a read/write access to this token
  uint8_t access_count;
  Token_Access_t accesses[MAX_ACCESSES]; //access list to the token for different threads
} Token_Descriptor_t;

Token_Access_t* get_access_from_token(int token);
/*
 * Datastructure to hold threads waiting for console data read asynchoronously
 */
typedef struct {
  L4_ThreadId_t tid; //tid waiting for the console read
  int number_bytes_left; //Number of bytes waiting to be read
  unsigned read_enabled : 1; //Flag to determine if read issued by the thread
} read_listener_t;

/*
 * Datastructure to store process table entries
 */
typedef struct {
  L4_ThreadId_t tid; //tid of the process
  unsigned  int size;		/* in pages */
  char	    command[MAX_EXEC_NAME];	/* Name of exectuable */
  int  token_table[MAX_TOKENS]; //Tokens opened by the process
  unsigned stime; //Creation time of the process
  L4_ThreadId_t waiting_tid[MAX_WAITING_TID]; //Tid of the threads waiting for this process to exit
} process_control_block_t;

/*
 * Datastructure to hold token values in asychronous elf load from file system
 */
struct token_process_t {
  char *elf_file; //elf file being read in from the file system
  unsigned int data_read; //size of data read
  unsigned int file_size; //total size of the file to be read 
  L4_ThreadId_t creating_process_tid; //tid of the process which issued the process create
  unsigned int process_table_index; //index in the process table which needs to be filled in
  struct cookie* fh; //cookie obtained in nfs_lookup of the file and used in nfs_read
} ;

/*
 * Function to update the size of process memory in pages invoked by the pager
 * on swapout/swapin/frame allocation
 */
extern void update_process_table_size(L4_ThreadId_t tid,unsigned increase);
extern void process_table_add_creation_entry(int index,L4_ThreadId_t newtid,unsigned success);
#endif
