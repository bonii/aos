#ifndef SOSRPC_H
#define SOSRPC_H

#include "../helpers/ipc_defs.h"
#include "l4.h"

#define MAX_TOKENS 10
#define MAX_ACCESSES 5
#define BUFFER_SIZE 50

void rpc_thread(void);

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

#endif
