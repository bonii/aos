#ifndef IPC_DEFS_H
#define IPC_DEFS_H

#include <stdint.h>

/**
 * Helper definitions that facilitate IPC.
 *
 */

/* Some IPC labels defined in the L4 documentation */
#define L4_PAGEFAULT	((L4_Word_t) -2)
#define L4_INTERRUPT	((L4_Word_t) -1)

/* Our own definitions for labels */
#define SOS_SYSCALL_SERIAL_SEND              ((L4_Word_t) 1)
#define SOS_SYSCALL_UNMAP_USER_ADDRESS_SPACE ((L4_Word_t) 2)
#define SOS_SYSCALL_FIND_RPC_THREAD          ((L4_Word_t) 7)
#define SOS_RPC_GET_TOKEN                    ((L4_Word_t) 8)
#define SOS_RPC_RELEASE_TOKEN                ((L4_Word_t) 9)
#define SOS_RPC_WRITE                        ((L4_Word_t) 10)
#define SOS_RPC_READ                         ((L4_Word_t) 11)
#define SOS_RPC_STAT                         ((L4_Word_t) 12)
#define SOS_RPC_GETDIRENT                    ((L4_Word_t) 13)
#define SOS_RPC_WRITE_PERMS                  ((L4_Word_t) 14)
#define SOS_RPC_TIMESTAMP                ((L4_Word_t) 15)
#define SOS_RPC_SLEEP                    ((L4_Word_t) 16)
#define SOS_RPC_PROCESS_CREATE               ((L4_Word_t) 17)
#define SOS_RPC_PROCESS_WAIT                 ((L4_Word_t) 18)
#define SOS_RPC_PROCESS_DELETE               ((L4_Word_t) 19)
#define SOS_RPC_PROCESS_STAT                 ((L4_Word_t) 20)
#define SOS_RPC_PROCESS_ID                   ((L4_Word_t) 21)

#define SEND_STAT_COMMAND                     ((L4_Word_t) 22)
#define SEND_STAT_DATA                        ((L4_Word_t) 23)
#define SEND_STAT_END                         ((L4_Word_t) 24)
#define SOS_SYSCALL_REMOVE_TID_PAGE           ((L4_Word_t) 25)

#define GETDIRENT_VALID_FILE                 ((L4_Word_t) 1)
#define GETDIRENT_FIRST_EMPTY_FILE           ((L4_Word_t) 2)
#define GETDIRENT_OUT_OF_RANGE               ((L4_Word_t) 3)

#define SOS_READ_CONSOLE                     ((L4_Word_t) 0)
#define SOS_READ_FILE                        ((L4_Word_t) 1)
#define SOS_READ_INVALID_TOKEN               ((L4_Word_t) 2)

#define SOS_WRITE_CONSOLE                     ((L4_Word_t) 3)
#define SOS_WRITE_FILE                        ((L4_Word_t) 4)
#define SOS_WRITE_INVALID_TOKEN               ((L4_Word_t) 5)

#define END_OF_READ_FILE_STATUS 10
/* Syslab tagging: "marshalling" and "unmarshalling" */
#define MAKETAG_SYSLAB(t)	(t << 4)
#define TAG_SYSLAB(t)	((short) L4_Label(t) >> 4)
#define TIME_SPLIT 100000000
/* how many words are sent per IPC call */
#define WORDS_SENT_PER_IPC 17

/* file names */
#define FILE_NAME_WORDS 16
#define FILE_NAME_SIZE (FILE_NAME_WORDS)*sizeof(L4_Word_t)
#define FILE_NAME_MODE_SIZE FILE_NAME_SIZE + sizeof(L4_Word_t)

#define MAX_FILES 32

/* file modes */
#define FM_WRITE 2
#define FM_READ  4
#define FM_EXEC  1
typedef uint8_t fmode_t;

#define O_RDONLY FM_READ
#define O_WRONLY FM_WRITE
#define O_RDWR   (FM_READ|FM_WRITE)

/* dprintf */
#define verbose 2

#endif
