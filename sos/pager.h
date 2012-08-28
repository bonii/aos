/*
 * Written by Vivek Shah
 */
#include <l4/types.h>
#include <l4/message.h>
#include "network.h"

/*
 * Function invoked by roottask on pagefault
 */
extern int pager(L4_ThreadId_t tid, L4_Msg_t *msg);

/*
 * Init function to initialise the page table entries
 */
extern L4_Word_t pager_init(L4_Word_t low, L4_Word_t high);

/*
 * Function to unmap all entries from the page table(used for milestone 2)
 */
extern void unmap_all(void);

/*
 * Function to initialise swapfile, create it if not present
 */
extern void initialise_swap(void);

/*
 * Swap creation callback function, received after swapfile lookup
 */
extern void initialise_swap_callback(uintptr_t token,int status,struct cookie *fh,fattr_t *attr);

/*
 * Callback function for nfs_write to swapfile, Issues a read from swap if necessary
 * Replies to the thread if only swapout need to be done and no swapin
 */
extern void pager_write_callback(uintptr_t token,int status, fattr_t *attr);

/*
 * Callback function to nfs_read from swapfile
 * Always replies to the thread
 */
extern void pager_read_callback(uintptr_t token,int status, fattr_t *attr, int bytes_read,char *data);

/*
 * Unmaps all the page table entries and pages and frees the frames for a tid
 */

extern void unmap_process(L4_ThreadId_t tid_killed);

/*
 * This function loads the entire elf file into the phsical frames and
 * maps fpages corresponding to virtual address in elf file to the process
 * Return -1 if the code could not be loaded
 */
extern int load_code_segment_virtual(char *elfFile,L4_ThreadId_t new_tid);

#define PTE_SENTINEL -1
#define UNDEFINED_MEMORY -1
#define VIRTUAL(addr) (addr >= 0x2000000)
#define MAX_SWAP_ENTRIES 1000
#define MAXI 10000
#define BASE_CODE_SEGMENT_ADDRESS 0x10000000
/*
 * Datastructure containing page table entries
 */
typedef struct {
  L4_ThreadId_t tid; 
  L4_Fpage_t pageNo; //page number of the virtual address
  unsigned referenced : 1; //reference bit for second chance page replacement algorithm
  unsigned dirty : 1; //dirty bit designating if the page was written to since last swap
  int write_bytes_transferred; //size of data transferred to swapfile
  int read_bytes_transferred; //size of data read in from swapfile
  unsigned being_updated : 1; //bit to designate if a page table entry is being updated
  unsigned error_in_transfer : 1; //bit to designate if there was an error in swapin/swapout
  unsigned pinned : 1; //pin a page to the page table to prevent eviction(code segment)
} sos_PTE;

/*
 * Datastructure to contain swap table entries
 */
typedef struct {
  L4_ThreadId_t tid;
  L4_Fpage_t pageNo; //page number of the virtual address
  int offset; //offset where the page is present in swap file
  int next_free; //index pointing to the next free entry in swap table which can be used
} sos_swap;

/*
 * Datastructure used as token during swap out and swap in
 */
struct page_token{
  L4_ThreadId_t destination_tid; //tid of the process to be mapped
  L4_Fpage_t destination_page; //virtual page of the process to be mapped
  L4_ThreadId_t source_tid; //tid of the process to be mapped out
  L4_Fpage_t source_page; //virtual address of the process to mapped out
  int pageIndex; //index in the page table to be updated
  int swapIndex; //index of swap where it is to be mapped out

  int swapIndexToBeReadIn; //index of swap which is to be mapped in
  int chunk_index; //index to demarcate the chunk number received in nfs read/write callback
  unsigned send_reply : 1; //bit to demarcate if a reply is to be sent to pagefaulting thread
  unsigned writeToSwapIssued :1; //bit to demarcate if there is a swapout prior to a swapin
} ;
