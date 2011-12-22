#include <l4/types.h>
#include <l4/message.h>
#include "network.h"

extern int pager(L4_ThreadId_t tid, L4_Msg_t *msg);
extern L4_Word_t pager_init(L4_Word_t low, L4_Word_t high);
extern void unmap_all(void);
extern void initialise_swap(void);
extern void initialise_swap_callback(uintptr_t token,int status,struct cookie *fh,fattr_t *attr);
extern void pager_write_callback(uintptr_t token,int status, fattr_t *attr);
extern void pager_read_callback(uintptr_t token,int status, fattr_t *attr, int bytes_read,char *data);
extern void unmap_process(L4_ThreadId_t tid_killed);

#define PTE_SENTINEL -1
#define UNDEFINED_MEMORY -1
#define VIRTUAL(addr) (addr >= 0x2000000)
#define MAX_SWAP_ENTRIES 1000
#define MAXI 10000

typedef struct {
    L4_ThreadId_t tid;
    L4_Fpage_t pageNo;
    unsigned referenced : 1;
    unsigned dirty : 1;
    int write_bytes_transferred;
    int read_bytes_transferred;
    unsigned being_updated : 1;
    unsigned error_in_transfer : 1;
    unsigned pinned : 1;
  //int pr_next; //It points to the next accessed entry 
} sos_PTE;

typedef struct {
    L4_ThreadId_t tid;
    L4_Fpage_t pageNo;
    int offset;
    int next_free;
} sos_swap;

struct page_token{
  L4_ThreadId_t destination_tid;
  L4_Fpage_t destination_page;
  L4_ThreadId_t source_tid;
  L4_Fpage_t source_page;
  int pageIndex;
  int swapIndex;

  int swapIndexToBeReadIn;
  int chunk_index;
  unsigned send_reply : 1;
  unsigned writeToSwapIssued :1;
} ;
