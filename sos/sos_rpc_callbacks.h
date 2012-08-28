/*
 * Written by Vivek Shah
 */
#ifndef SOS_RPC_CALLBACKS_H
#define SOS_RPC_CALLBACKS_H

#include <string.h>
#include "network.h"
#include "../helpers/ipc_defs.h"

/*
 * Callback function for nfs lookup, loads the file attributes if found
 */
void nfs_lookup_callback(uintptr_t token, int status, struct cookie* fh, fattr_t* attr);

/*
 * Callback function for nfs read. It reads directory contents incrementally issuing nfs readdirs
 */
void nfs_readdir_callback(uintptr_t token, int status, int num_entries, struct nfs_filename* filenames, int next_cookie);

/*
 * Callback function to get_file_handle, loads the cookie which is to be used for subsequent
 * nfs calls
 */
void nfs_getcookie_callback(uintptr_t token, int status, struct cookie* fh, fattr_t* attr);

/*
 * Callback function for nfs read, it replies to the thread waiting for the read bytes character
 * by character
 */
void nfs_read_callback(uintptr_t t, int status, fattr_t *attr, int bytes_read, char *data);

/*
 * Callback function for nfs write 
 */
void nfs_write_callback(uintptr_t token, int status, fattr_t *attr);

/*
 * Callback function for process creation elf file lookup
 */
void process_create_lookup_callback(uintptr_t token, int status, struct cookie* fh, fattr_t* attr);

/*
 * Callback function for process create nfs read of the elf file
 */
void process_create_read_callback(uintptr_t token, int status, fattr_t *attr, int bytes_read, char *data);

typedef struct { char chars[FILE_NAME_SIZE]; } filename_t;

extern int filename_iterator;
extern filename_t files[MAX_FILES];

#endif
