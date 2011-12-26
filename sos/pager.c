/****************************************************************************
 *
 *      $Id: pager.c,v 1.4 2003/08/06 22:52:04 benjl Exp $
 *
 *      Description: Example pager for the SOS project.
 *
 *      Author:			Godfrey van der Linden
 *      Original Author:	Ben Leslie
 *
 ****************************************************************************/


//
// Pager is called from the syscall loop whenever a page fault occurs. The current
// implementation has an inverted page table without hashing
//
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <l4/types.h>
#include <stdlib.h>
#include <l4/map.h>
#include <l4/misc.h>
#include <l4/space.h>
#include <l4/thread.h>
#include <l4/cache.h>
#include <l4/ipc.h>
#include "pager.h"
#include "libsos.h"
#include "frames.h"
#include "sos_rpc.h"
#include <elf/elf.h>
static char SWAPFILE[] = "swapfile";

static sos_PTE* page_table = 0;
static L4_Word_t new_low = 0;
static int numPTE = 0;
static sos_swap swap_table[MAX_SWAP_ENTRIES];
static int swapInitialised = 0;
static struct cookie* swapcookie = 0;
static int swap_file_size = 0;
static int head_free_swap = 0;
static int send = 1;

/*
 * This function loads the entire elf file into the phsical frames and
 * maps fpages corresponding to virtual address in elf file to the process
 */
int load_code_segment_virtual(char *elfFile,L4_ThreadId_t new_tid) {
  uint32_t min[2];
  uint32_t max[2];
  elf_getMemoryBounds(elfFile, 0, (uint64_t*)min, (uint64_t*)max);
  //Now we need to reserve memory between min and max
  L4_Word_t lower_address = ((L4_Word_t) min[1] / PAGESIZE) * PAGESIZE; 
  L4_Word_t upper_address = ((L4_Word_t) max[1] / PAGESIZE) * PAGESIZE;
 
  while(lower_address <= upper_address) {
    L4_Word_t frame = frame_alloc();
    if(!frame) {
      //Oops out of frames
      unmap_process(new_tid);
      return -1;
    } else {
      L4_Fpage_t targetpage = L4_FpageLog2(lower_address,12);
      lower_address += PAGESIZE;
      //Now map fpage
      L4_Set_Rights(&targetpage,L4_FullyAccessible);
      L4_PhysDesc_t phys = L4_PhysDesc(frame, L4_DefaultMemory);
      //Map the frame to root task but enter entries in pagetable with tid since we will update the mappings once elf loading is done
      if (L4_MapFpage(L4_Myself(), targetpage, phys) ) {
	page_table[(frame-new_low)/PAGESIZE].tid = new_tid;
	page_table[(frame-new_low)/PAGESIZE].pinned = 1;
	page_table[(frame-new_low)/PAGESIZE].pageNo = targetpage;
      } else {
	unmap_process(new_tid);
      }
    }
  }
  //Now we have mapped the pages, now load elf_file should work with the virtual addresses
  if(elf_loadFile(elfFile,0) == 1) {
      //Elffile was successfully loaded
      //Map the fpages which were previously mapped to Myself to the tid
    for(int i=0;i<numPTE;i++) {
      if(L4_ThreadNo(new_tid) == L4_ThreadNo(page_table[i].tid)) {
	//Now remap the pages which were mapped to root task to the new tid
	L4_UnmapFpage(L4_Myself(),page_table[i].pageNo);
	L4_PhysDesc_t phys = L4_PhysDesc(new_low + i * PAGESIZE, L4_DefaultMemory);
	if(!L4_MapFpage(new_tid, page_table[i].pageNo, phys)) {
	  unmap_process(new_tid);
	  return -1;
	}
      }
    }
  } else {
    unmap_process(new_tid);
  }
  //Remove later
  L4_CacheFlushAll();
  return 0;
}

/*
 * Internal function to write a page table location to a swap location and to invoke a read
 * from swap if necessary which is invoked by the nfs write callback
 */
static void writeToSwap(int index,int readSwapIndex,L4_ThreadId_t tid,L4_Fpage_t fpage) {
  if(!swapInitialised) {
    initialise_swap();
  }
  //Now we need to write the contents of pagetable index to swap file
  //Get a free location in the swap table
  int swap_index = head_free_swap;
  head_free_swap = swap_table[head_free_swap].next_free;

  if(swap_index >= MAX_SWAP_ENTRIES || swap_index < 0) {
    printf("Panic !!! Swap table full ! No more swapping possible!\n");
    return;
  }

  //If a swap location was once used overwrite it to keep a constant swap file size
  if(swap_table[swap_index].offset == PTE_SENTINEL)  {
    swap_table[swap_index].offset = swap_file_size;
    swap_file_size += PAGESIZE;
  }

  //Now we need to write the file,
  struct page_token *token_val;
  page_table[index].being_updated = 1;
  page_table[index].write_bytes_transferred = 0;
  page_table[index].error_in_transfer = 0;
  //Unmap before the first nfs write
  L4_UnmapFpage(page_table[index].tid,page_table[index].pageNo);

  for(int i=0;i<PAGESIZE/NFS_WRITE_SIZE;i++) {
    //Set the token val
    token_val = (struct page_token *) malloc(sizeof(struct page_token));  
    //printf("After malloc of page_token for i %d\n",i);
    token_val -> pageIndex = index;
    token_val -> swapIndex = swap_index;
    //Send reply only if there is no read from swap
    token_val -> send_reply = (readSwapIndex == -1) ? 1 : 0 ;
    token_val -> chunk_index = i;
    token_val -> destination_tid = tid;
    token_val -> destination_page = fpage;
    token_val -> source_tid = page_table[index].tid;
    token_val -> source_page = page_table[index].pageNo;
    token_val -> swapIndexToBeReadIn = readSwapIndex;
    //All nfs writes for each chunk are issued at once
    nfs_write(swapcookie,swap_table[swap_index].offset+i*NFS_WRITE_SIZE,NFS_WRITE_SIZE,(void *)(new_low + index*PAGESIZE + i*NFS_WRITE_SIZE),pager_write_callback,(uintptr_t)token_val);
  }
  send = 0;
}

/*
 * Internal function to read from swap file into a page table entry
 */
static void readFromSwap(int swapIndex,int pagetableIndex, int writeToSwap,L4_ThreadId_t tid,L4_Fpage_t fpage) {
  //Initialise swap if not initialised
  if(!swapInitialised) {
    initialise_swap();
  }
  struct page_token *token_val;
  
  page_table[pagetableIndex].read_bytes_transferred = 0;
  page_table[pagetableIndex].being_updated = 1;
  page_table[pagetableIndex].error_in_transfer = 0;

  for(int i=0;i<PAGESIZE/NFS_READ_SIZE;i++) {
    //Set the token value
    token_val = (struct page_token *) malloc(sizeof(struct page_token));  
    token_val -> pageIndex = pagetableIndex;
    token_val -> swapIndex = swapIndex;
    //Read will always be the last to reply so it always replies
    token_val -> send_reply = 1;
    token_val -> chunk_index = i;
    //Set bit whether the read from swap occurs after a swap write
    token_val -> writeToSwapIssued = writeToSwap;
    token_val -> destination_tid = tid;
    token_val -> destination_page = fpage;
    token_val -> source_tid = page_table[pagetableIndex].tid;
    token_val -> source_page = page_table[pagetableIndex].pageNo;
    //Issue all the nfs reads for the chunks at once
    nfs_read(swapcookie,swap_table[swapIndex].offset+i*NFS_READ_SIZE,NFS_READ_SIZE,pager_read_callback,(uintptr_t) token_val);
  }
  send = 0;
}

/*
 * Returns the page table index to be evicted and the static variable 
 * ensures it remembers the last search index for the next eviction
 * Uses the second chance page replacement algorithm
 */
static int evictPage(void) {
  static int evictIndex = 0;
  int copyIndex = evictIndex;
  int counter = 0;
  while(1) {
    counter++;
    //Check that a being updated and pinned entry is not evicted
    if((L4_ThreadNo(page_table[evictIndex].tid) == L4_ThreadNo(L4_nilthread)) || (page_table[evictIndex].referenced == 0 && page_table[evictIndex].being_updated == 0 && page_table[evictIndex].pinned == 0))
      return evictIndex;
    else if(page_table[evictIndex].being_updated == 0 && page_table[evictIndex].pinned == 0){
      page_table[evictIndex].referenced = 0;
      L4_UnmapFpage(page_table[evictIndex].tid,page_table[evictIndex].pageNo);
    } else if (page_table[evictIndex].being_updated == 1) {
      copyIndex = (copyIndex + 1) % numPTE;
    }
    //Check if we have done one full cycle of page table search without an eviction owing to 
    //updation then bail else we go into an infinite loop
    if(counter == numPTE) {
      if(copyIndex == evictIndex) {
	return -1;
      }
      numPTE = 0;
      copyIndex = evictIndex;
    }
    evictIndex = (evictIndex + 1) % numPTE;
  }
}

/*
 * This function is the controller function which maps virtual addresses to physical frames. 
 */
static L4_Word_t mapAddress(L4_ThreadId_t tid, L4_Fpage_t fpage, int swapIndex)
{    
    // allocate a new frame
    int evicted_not_dirty = 0;
    L4_Word_t physAddress = frame_alloc();
    int i = 0;
    //int head = 0, oldHead = 0;
    if(physAddress == 0) {
      int evicted = evictPage();
      //If page replacement aborted if all pages in page table are currently being updated, abort
      if(evicted == -1) {
	return -1;
      }

      if(page_table[evicted].dirty == 1) {
	//We need to write it to swap
        writeToSwap(evicted,swapIndex,tid,fpage);
      } else {
	evicted_not_dirty = 1;
      }
      i = evicted;
    } else {
      i = (physAddress - new_low)/PAGESIZE;
    }
    if(physAddress != 0 || evicted_not_dirty) {
      //We have a frame avialable to us or a swapout is not necessary
      if(swapIndex != -1) {
	//We need to read from the swap and overwrite the physical frame with it
	//We need to read from the swap and signal to it that there was no swapout
	readFromSwap(swapIndex,i,0,tid,fpage);
      } else {
	//We do not have to read from swap, we just overwrite the frame
	//Page was not swapped out neither swapped in
	page_table[i].tid = tid;
	page_table[i].pageNo = fpage;
	page_table[i].being_updated = 0;
	page_table[i].referenced = 1;
	page_table[i].dirty = 0;
	send = 1;
	L4_Set_Rights(&fpage,L4_Readable);  
	L4_PhysDesc_t phys = L4_PhysDesc(new_low + i * PAGESIZE, L4_DefaultMemory);
	//update the size of the process table
	update_process_table_size(tid,1);
	L4_MapFpage(tid, fpage,phys);
      }
    }
    return physAddress;
}

/*
 * Internal function to search the page table
 * Does a linear search but for 5000-6000 entries the performance is not bad
 */
static int isInPage(L4_ThreadId_t tid, L4_Fpage_t fpage)
{
  for(int i=0;i<numPTE;i++) {
        if (L4_ThreadNo(page_table[i].tid) == L4_ThreadNo(tid)
            && page_table[i].pageNo.X.b == fpage.X.b)
        {
            printf("old page found in page at %d (%lx, %d)\n", i, L4_ThreadNo(page_table[i].tid), page_table[i].pageNo.X.b);
            return i;
        }   
  }  
  return -1;
}

/*
 * Internal function to search the swap table
 */
static int isInSwap(L4_ThreadId_t tid, L4_Fpage_t fpage)
{
  for(int i=0;i<MAX_SWAP_ENTRIES;i++) {
        if (L4_ThreadNo(swap_table[i].tid) == L4_ThreadNo(tid)
            && swap_table[i].pageNo.X.b == fpage.X.b)
        {
            printf("old page found in swap at %d (%lx, %d)\n", i, L4_ThreadNo(swap_table[i].tid), swap_table[i].pageNo.X.b);
            return i;
        }     
  }
  return -1;
}

/*
 * Init function to initialise the page table entries
 */
L4_Word_t
pager_init(L4_Word_t low, L4_Word_t high)
{
  //The frames have been used to set up page table entries as well
    page_table = (sos_PTE*) low;
    // Use a simple algaebric formula to calculate optimum size of page table for the
    // amount of memory available
    //new_low points to the memory to be used now, memory low -> new_low is the pagetable
    new_low = ((double)high*sizeof(sos_PTE)+PAGESIZE*(double)low)/
              (double)(PAGESIZE+sizeof(sos_PTE));
    // align it
    new_low = (new_low/PAGESIZE)*PAGESIZE + PAGESIZE;          
    numPTE = (high-new_low)/PAGESIZE;

    printf("low: %lx new_low: %lx high: %lx numPTE: %d \n", low, new_low, high, numPTE);
    printf("value of swap memory %p \n",swap_table);
    // initialize the empty page table.
    for (int i = 0; i < numPTE; i++)
    {
        page_table[i].tid = L4_nilthread;
	page_table[i].referenced = 0;
	page_table[i].dirty = 0;
	page_table[i].being_updated = 0;
	page_table[i].error_in_transfer = 0;
        page_table[i].pinned = 0;
    }
    
    for(int i=0;i<MAX_SWAP_ENTRIES;i++) {
        swap_table[i].tid = L4_nilthread;
	swap_table[i].offset = PTE_SENTINEL;
	//Initially all entries are free so each points to the next one in the table
	swap_table[i].next_free = i+1;
    }
    // add a guard page against stack overflows and let it map to 0 
    L4_Word_t guardPage = 0x7000000;
    L4_PhysDesc_t phys = L4_PhysDesc(0, L4_DefaultMemory);
    L4_Fpage_t targetFpage = L4_FpageLog2(guardPage, 12);
    L4_Set_Rights(&targetFpage, L4_Readable);
    if ( !L4_MapFpage(L4_Myself(), targetFpage, phys) ) {
        sos_print_error(L4_ErrorCode());
        printf(" Can't map guard page\n");
    }
    return new_low;
}

/*
 * Function to unmap all entries from the page table(used for milestone 2)
 */
void
unmap_all()
{
    for (int i = 0; i < numPTE; i++)
    {
        if (L4_ThreadNo(page_table[i].tid) != L4_ThreadNo(L4_nilthread))
        {
            L4_Fpage_t toUnmap = page_table[i].pageNo;
            L4_UnmapFpage(page_table[i].tid, toUnmap);
        }
    }
}

/*
 * Swap creation callback function after nfs_lookup of swapfile
 */
void initialise_swap_callback(uintptr_t token,int status,struct cookie *fh, fattr_t *attr) {
  //Now we need to call nfs
  int create_issued = 0;
  if(status != 0) {
    //Swapfile does not exist so create it
    sattr_t attributes;
    attributes.size = 0;
    attributes.mode = 0x06;
    nfs_create(&mnt_point,SWAPFILE,&attributes,initialise_swap_callback,token);
    create_issued = 1;
    //Add some delay here
    return;
  }
  if(create_issued || status == 0) {
    printf("\nSwapfile created !!\n");
    //Need to copy the cookie into a memory location
    swapcookie = (struct cookie *)malloc(sizeof(struct cookie));
    swapInitialised = 1;
    memcpy(swapcookie,fh,sizeof(struct cookie));
  }
}

/*
 * Callback function to nfs_write to swapfile, Issues a read from swap if necessary
 * Replies to the thread if only swapout need to be done and no swapin
 */
void pager_write_callback(uintptr_t token, int status, fattr_t *attr) {
  struct page_token *token_val = (struct page_token *) token;
  int pageIndex = token_val -> pageIndex;
  int swapIndex = token_val -> swapIndex;

  if(status != 0) {
    //Something bad happened so we cannot overwrite the page content, lets delay for 
    //next page fault
    page_table[pageIndex].error_in_transfer = 1;
  }
  
  //Increment the bytes transferred for each callback
  page_table[pageIndex].write_bytes_transferred += NFS_WRITE_SIZE;
  
  //If all callbacks have been received without an error we have a successful write
  if(page_table[pageIndex].write_bytes_transferred == PAGESIZE) {
    if(!page_table[pageIndex].error_in_transfer) {
        //Update the swap table
	swap_table[swapIndex].tid = token_val -> source_tid;
	swap_table[swapIndex].pageNo = token_val -> source_page;
	
	//Check if swapin needs to be done
	if(token_val -> send_reply) {
	  L4_PhysDesc_t phys = L4_PhysDesc(new_low + pageIndex * PAGESIZE, L4_DefaultMemory);
          L4_Set_Rights(&(token_val -> destination_page),L4_Readable);  
	  //Map the page to the new tid
	  L4_MapFpage(token_val -> destination_tid, token_val -> destination_page,phys);
	  //Update page table
	  page_table[pageIndex].tid = token_val -> destination_tid;
	  page_table[pageIndex].pageNo = token_val -> destination_page;
	  page_table[pageIndex].referenced = 1;
	  page_table[pageIndex].dirty = 0;
	} else {
	  //There is a read coming up
          readFromSwap(token_val -> swapIndexToBeReadIn,pageIndex,1,token_val -> destination_tid,token_val -> destination_page);
	}
	
    } else {
      // If there is an error in transfer, currently we have nothing to do
      //Since this write was invalidated we undo the change
      swap_file_size -= PAGESIZE;
      //Undo the swap that was allocated
      swap_table[swapIndex].next_free = head_free_swap;
      head_free_swap = swapIndex;
    }
    //If there was an error in transfer (we dont issue swapin even if needed) or
    //there was no swapin after swapoff we reply to faulting tid
    if(token_val -> send_reply || page_table[pageIndex].error_in_transfer) {
      //We clear the error in transfer value and being updated since the work is done
      page_table[pageIndex].being_updated = page_table[pageIndex].error_in_transfer = 0;
      //Now send the reply message
      //Update the process table size 
      update_process_table_size(token_val -> source_tid,0);
      L4_Msg_t msg;
      L4_MsgClear(&msg);
      L4_MsgLoad(&msg);
      L4_Reply(token_val ->destination_tid);
    }
  }
  //Free up the token
  free(token_val);
}

/*
 * Function to initialise swapfile, create it if not present
 */
void initialise_swap() {
  nfs_lookup(&mnt_point,SWAPFILE,initialise_swap_callback,L4_Myself().raw);
}

/*
 * Callback function to nfs_read from swapfile
 * Always replies to the thread
 */
void pager_read_callback(uintptr_t token,int status, fattr_t *attr, int bytes_read,char *data) {
  struct page_token *token_val = (struct page_token *) token;
  int pagetableIndex = token_val -> pageIndex;
  int byte_index = token_val -> chunk_index;
  int swapIndex = token_val -> swapIndex;

  if(status != 0) {
    page_table[pagetableIndex].error_in_transfer = 1;
  } else {
    //Copy the data read to memory
    char *memstart = (char *) (new_low + pagetableIndex * PAGESIZE + byte_index*NFS_READ_SIZE); 
    memcpy(memstart,data,NFS_READ_SIZE);
  }
    
  page_table[pagetableIndex].read_bytes_transferred += NFS_READ_SIZE;

  //Check if all the callbacks have been received
  if(page_table[pagetableIndex].read_bytes_transferred == PAGESIZE) {
    if(page_table[pagetableIndex].error_in_transfer == 1) {
      //The memory in pagetable is inconsistent so the best thing would be to mark the 
      //page table entry as unreferenced and hopefully its evicted soon
      //If this occurs for a free frame its best to free the frame
      //This condition is not required we would always want to free the frame(think and remove)
      if(!token_val -> writeToSwapIssued) {
        frame_free(new_low + pagetableIndex * PAGESIZE);
      }
      //Unmap the page table page whose memory we corrupted
      L4_UnmapFpage(page_table[pagetableIndex].tid,page_table[pagetableIndex].pageNo);

      page_table[pagetableIndex].tid = L4_nilthread;
      page_table[pagetableIndex].referenced = 0;
      page_table[pagetableIndex].dirty = 0;

    } else {
      //Free up the swap entry from which we read in
      swap_table[swapIndex].tid = L4_nilthread;
      swap_table[swapIndex].next_free = head_free_swap;
      head_free_swap = swapIndex;
      //Update page table
      page_table[pagetableIndex].tid = token_val -> destination_tid;
      page_table[pagetableIndex].pageNo = token_val -> destination_page;
      page_table[pagetableIndex].referenced = 1;
      page_table[pagetableIndex].dirty = 0;
      //Unmap the page which was written out
      if(token_val -> writeToSwapIssued) {
        L4_UnmapFpage(token_val -> source_tid,token_val -> source_page);
      }
      L4_Set_Rights(&(token_val -> destination_page),L4_Readable);  
      L4_PhysDesc_t phys = L4_PhysDesc(new_low + pagetableIndex * PAGESIZE, L4_DefaultMemory);
      L4_MapFpage(token_val -> destination_tid, token_val -> destination_page, phys);
      L4_CacheFlushAll();
      //Everything went fine
    }
    L4_Msg_t msg;
    L4_MsgClear(&msg);
    L4_MsgLoad(&msg);
    L4_Reply(token_val -> destination_tid);
    //Update the process table size
    update_process_table_size(token_val -> destination_tid,1);
    page_table[pagetableIndex].being_updated = page_table[pagetableIndex].error_in_transfer = 0;
  }
  free(token_val);
}

/*
 * Unmaps all the page table entries and pages and frees the frames for a tid
 */
void unmap_process(L4_ThreadId_t tid_killed) {
  //Clear page table
  for(int i=0;i<numPTE;i++) {
    if(L4_ThreadNo(page_table[i].tid) == L4_ThreadNo(tid_killed)) {
	L4_UnmapFpage(page_table[i].tid,page_table[i].pageNo);      
	frame_free(new_low + i * PAGESIZE);
        page_table[i].tid = L4_nilthread;
	page_table[i].referenced = 0;
	page_table[i].dirty = 0;
	page_table[i].being_updated = 0;
	page_table[i].error_in_transfer = 0;
        page_table[i].pinned = 0;
    }
  }
  
  //Clear swap table
  for(int i=0;i<MAX_SWAP_ENTRIES;i++) {
    if(L4_ThreadNo(swap_table[i].tid) == L4_ThreadNo(tid_killed)) {
        swap_table[i].tid = L4_nilthread;
	swap_table[i].next_free = head_free_swap;
	head_free_swap = i;
    }
  }
  //Remove later not required
  L4_CacheFlushAll();
}

/*
 * Function invoked by roottask on pagefault
 */
int
pager(L4_ThreadId_t tid, L4_Msg_t *msgP)
{
    send = 1;
    // Get the faulting address
    L4_Word_t addr = L4_MsgWord(msgP, 0);
    L4_Word_t physicalAddress = 0;
    L4_Word_t permission = 0;
    L4_MsgTag_t tag;
    // Alignment
    addr = (addr / PAGESIZE)*PAGESIZE;
    tag = L4_MsgMsgTag(msgP);
    L4_Word_t access_type = L4_Label(tag) & 0x07;

    printf("pager invoked addr=%lx by %lx %lx for access 0x%lx\n", addr,L4_ThreadNo(tid),tid.raw,access_type);

    // Construct fpage IPC message
    L4_Fpage_t targetFpage = L4_FpageLog2(addr, 12);
    
    if(VIRTUAL(addr)) 
    {
      if(addr >= BASE_CODE_SEGMENT_ADDRESS) {
	//Code segment
	int inPage = isInPage(tid,targetFpage);
	if(inPage == -1) {
	  //It should be in page table so this should not happen
	  printf("Panic !!! Cannot load the code segment");
	} else {
	  physicalAddress = new_low + inPage*PAGESIZE;
	  permission = L4_FullyAccessible;
	}
      } else {
	//Heap and stack
    	int inPage = isInPage(tid, targetFpage);
    	if (inPage == -1)
    	{
	  //We need to check if the page is in swap
	    inPage = isInSwap(tid,targetFpage);
	    mapAddress(tid, targetFpage,inPage);
	    //We dont need to map any addresses here as mapAddresses maps the addresses
	    return send;
    	} else {
    	    physicalAddress = new_low+inPage*PAGESIZE;
	    targetFpage = page_table[inPage].pageNo;
	    page_table[inPage].referenced = 1;
	    if(access_type & L4_Writable) {
	      //We now need to set the dirty bit and provide read write access
	      page_table[inPage].dirty = 1;
	      permission = L4_ReadWriteOnly;
	    } else {
	      permission = L4_Readable;
	    }
    	}
    	
      }
    } else {
        // we need to map physical addresses 1:1
        physicalAddress = addr;
        if(addr < new_low) {
	        // This is beyond the low memory range ie the page table
	        // and some other addresses which is below the low range
	        permission = L4_FullyAccessible;
        } else {
	        // This would be the code segment between the new_low and high
	        permission = L4_Readable;
        }
    } 
    
    L4_Set_Rights(&targetFpage,permission);
    L4_PhysDesc_t phys = L4_PhysDesc(physicalAddress, L4_DefaultMemory);

    if ( !L4_MapFpage(tid, targetFpage, phys) ) {
        sos_print_error(L4_ErrorCode());
        printf(" Can't map page at %lx\n", addr);
    }
    return send;
}

