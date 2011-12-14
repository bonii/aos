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

extern void rpc_thread(void);
extern void set_address_binfo(L4_BootRec_t *list[], L4_Word_t user_stack_address, int number_of_exec);

typedef struct {
    L4_ThreadId_t tid;
    fmode_t mode;
    int read_offset;
    int write_offset;
    int max_read_offset;
} Token_Access_t;

typedef struct {
    char filename[FILE_NAME_SIZE];
    L4_Word_t token;
    uint8_t read_count, write_count;
    uint8_t access_count;
    Token_Access_t accesses[MAX_ACCESSES];
} Token_Descriptor_t;

Token_Access_t* get_access_from_token(int token);

typedef struct {
  L4_ThreadId_t tid;
  int number_bytes_left;
  unsigned read_enabled : 1;
} read_listener_t;

typedef struct {
  L4_ThreadId_t tid;
  unsigned  size;		/* in pages */
  char	    command[MAX_EXEC_NAME];	/* Name of exectuable */
  int  token_table[MAX_TOKENS];
  L4_ThreadId_t waiting_tid[MAX_WAITING_TID];
} process_control_block_t;


#endif
