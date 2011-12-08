#ifndef SOS_RPC_CALLBACKS_H
#define SOS_RPC_CALLBACKS_H

#include <string.h>
#include "network.h"
#include "../helpers/ipc_defs.h"

void nfs_lookup_callback(uintptr_t token, int status, struct cookie* fh, fattr_t* attr);
void nfs_readdir_callback(uintptr_t token, int status, int num_entries, struct nfs_filename* filenames, int next_cookie);
void nfs_getcookie_callback(uintptr_t token, int status, struct cookie* fh, fattr_t* attr);
void nfs_read_callback(uintptr_t t, int status, fattr_t *attr, int bytes_read, char *data);
void nfs_write_callback(uintptr_t token, int status, fattr_t *attr);
typedef struct { char chars[FILE_NAME_SIZE]; } filename_t;

extern int filename_iterator;
extern filename_t files[MAX_FILES];

#endif
