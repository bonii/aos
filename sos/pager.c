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
// Pager is called from the syscall loop whenever a page fault occurs. The
// current implementation simply maps whichever pages are asked for.
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

static char SWAPFILE[] = "swapfile";

//static head_of_table = 0;
//static tail_of_pagetable = 0;

static sos_PTE* page_table = 0;
static L4_Word_t new_low = 0;
static int numPTE = 0;
//static int most_recently_accessed = PTE_SENTINEL;
static sos_swap swap_table[MAX_SWAP_ENTRIES];
static int swapInitialised = 0;
static struct cookie* swapcookie = 0;
static int swap_file_size = 0;
static int head_free_swap = 0;
static int send = 1;

static void writeToSwap(int index,int readSwapIndex,L4_ThreadId_t tid,L4_Fpage_t fpage) {
  if(!swapInitialised) {
    initialise_swap();
  }
  //Now we need to write the contents of pagetable index to swap file
  int swap_index = head_free_swap;
  printf("Swap index %d",swap_index);

  head_free_swap = swap_table[head_free_swap].next_free;
  if(swap_index >= MAX_SWAP_ENTRIES || swap_index < 0) {
    printf("Panic !!! Swap table full ! No more swapping possible!\n");
    return;
  }
  printf("Here 222");
  if(swap_table[swap_index].offset == PTE_SENTINEL)  {
    swap_table[swap_index].offset = swap_file_size;
    swap_file_size += PAGESIZE;
  }
  //swap_table[swap_index].size = PAGESIZE;
  //Now we need to write the file
  printf("Writing out %lx memory\n",(new_low + index*PAGESIZE));
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
    token_val -> send_reply = (readSwapIndex == -1) ? 1 : 0 ;
    token_val -> chunk_index = i;
    token_val -> destination_tid = tid;
    token_val -> destination_page = fpage;
    token_val -> source_tid = page_table[index].tid;
    token_val -> source_page = page_table[index].pageNo;
    token_val -> swapIndexToBeReadIn = readSwapIndex;
    int returnval = nfs_write(swapcookie,swap_table[swap_index].offset+i*NFS_WRITE_SIZE,NFS_WRITE_SIZE,(void *)(new_low + index*PAGESIZE + i*NFS_WRITE_SIZE),pager_write_callback,(uintptr_t)token_val);
    if (0) printf("nfs_write returned %d\n",returnval);
  }
  send = 0;
  //nfs_write(swapcookie,swap_table[swap_index].offset,PAGESIZE,"abc",pager_write_callback,page_table[index].tid.raw);
  printf("debug 10 ...");
}

static void readFromSwap(int swapIndex,int pagetableIndex, int writeToSwap,L4_ThreadId_t tid,L4_Fpage_t fpage) {
  if(!swapInitialised) {
    initialise_swap();
  }
  struct page_token *token_val;
  
  //We are only here if there was no error in write or there was no write to swap done
  page_table[pagetableIndex].read_bytes_transferred = 0;
  page_table[pagetableIndex].being_updated = 1;
  page_table[pagetableIndex].error_in_transfer = 0;

  for(int i=0;i<PAGESIZE/NFS_READ_SIZE;i++) {
    token_val = (struct page_token *) malloc(sizeof(struct page_token));  
    token_val -> pageIndex = pagetableIndex;
    token_val -> swapIndex = swapIndex;
    //Read will always be the last to reply
    token_val -> send_reply = 1;
    token_val -> chunk_index = i;
    token_val -> writeToSwapIssued = writeToSwap;
    token_val -> destination_tid = tid;
    token_val -> destination_page = fpage;
    token_val -> source_tid = page_table[pagetableIndex].tid;
    token_val -> source_page = page_table[pagetableIndex].pageNo;
    //printf("Reading offset in file %d, pagetableIndex %d\n",swap_table[swapIndex].offset + i*NFS_READ_SIZE,pagetableIndex);
    nfs_read(swapcookie,swap_table[swapIndex].offset+i*NFS_READ_SIZE,NFS_READ_SIZE,pager_read_callback,(uintptr_t) token_val);
  }
  send = 0;
  printf("Reading from swap\n");
}

//Returns the page table index to be evicted and the static variable 
//ensures it remembers the last search index
static int evictPage(void) {
  static int evictIndex = 0;
  int copyIndex = evictIndex;
  int counter = 0;
  while(1) {
    counter++;
    if((L4_ThreadNo(page_table[evictIndex].tid) == L4_ThreadNo(L4_nilthread)) || (page_table[evictIndex].referenced == 0 && page_table[evictIndex].being_updated == 0 && page_table[evictIndex].pinned == 0))
      return evictIndex;
    else if(page_table[evictIndex].being_updated == 0 && page_table[evictIndex].pinned == 0){
      page_table[evictIndex].referenced = 0;
      L4_UnmapFpage(page_table[evictIndex].tid,page_table[evictIndex].pageNo);
    } else if (page_table[evictIndex].being_updated == 1) {
      copyIndex = (copyIndex + 1) % numPTE;
    }
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

static L4_Word_t mapAddress(L4_ThreadId_t tid, L4_Fpage_t fpage, int swapIndex)
{    
    // allocate a new frame
    printf("debug 7 ....");
    int evicted_not_dirty = 0;
    L4_Word_t physAddress = frame_alloc();
    printf("after debug 7...");
    int i = 0;
    //int head = 0, oldHead = 0;
    if(physAddress == 0) {
      printf("debug 3 ...");
      //int evicted = (L4_ThreadNo(tid) * fpage.X.b) % numPTE;
      int evicted = evictPage();
      //If we aborted if all pages in page table are currently being updated
      if(evicted == -1) {
	return -1;
      }

      printf("Woo hoo");
      if(page_table[evicted].dirty == 1) {
	//We need to write it to swap
	printf("debug 9 ...");
        writeToSwap(evicted,swapIndex,tid,fpage);
        L4_CacheFlushAll();
      } else {
	evicted_not_dirty = 1;
      }
      i = evicted;
      //physAddress = new_low + evicted * PAGESIZE;
    } else {
      printf("physical address %lx new low %lx",physAddress,new_low);
      i = (physAddress - new_low)/PAGESIZE;
      printf("debug 5 ...");
    }
    printf("debug 6 ...");
    printf("allocating page %d (%lx, %d)\n", i, L4_ThreadNo(tid), fpage.X.b);
    //page_table[i].pr_next = most_recently_accessed;
    /* if(tail_of_table == PTE_SENTINEL) {
      tail_of_table = i;
    }
    page_table[i].pr_next = head_of_table;
    head_of_table = i;*/
    //most_recently_accessed = i;
    if(swapIndex != -1 && physAddress == 0) {
      //We need to read from the swap and overwrite the physical frame with it
      printf("Value of phys address %lx\n",physAddress);
      //If the physical address returned by frame_alloc was 0 it means a page was evicted
      readFromSwap(swapIndex,i,((physAddress == 0) ? 1 : 0 ),tid,fpage);
      L4_CacheFlushAll();
    } else if(physAddress != 0 || evicted_not_dirty) {
      printf("Foo Bar\n");
      //Page was not swapped out neither swapped in
     page_table[i].tid = tid;
     page_table[i].pageNo = fpage;
     page_table[i].being_updated = 0;
     page_table[i].referenced = 1;
     page_table[i].dirty = 0;
     send = 1;
     L4_Set_Rights(&fpage,L4_Readable);  
     L4_PhysDesc_t phys = L4_PhysDesc(new_low + i * PAGESIZE, L4_DefaultMemory);
     update_process_table_size(tid,1);
     L4_MapFpage(tid, fpage,phys);
     L4_CacheFlushAll();
    }
    return physAddress;
}

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

L4_Word_t
pager_init(L4_Word_t low, L4_Word_t high)
{
    page_table = (sos_PTE*) low;
    // good luck figuring this out :D
    new_low = ((double)high*sizeof(sos_PTE)+PAGESIZE*(double)low)/
              (double)(PAGESIZE+sizeof(sos_PTE));
    // alignment
    new_low = (new_low/PAGESIZE)*PAGESIZE + PAGESIZE;          
    // the number of PTEs can be calculated in two ways.
    numPTE = (high-new_low)/PAGESIZE;

    printf("low: %lx new_low: %lx high: %lx numPTE: %d \n", low, new_low, high, numPTE);
    printf("value of swap memory %p \n",swap_table);
    // initialize the empty page table.
    for (int i = 0; i < numPTE; i++)
    {
        page_table[i].tid = L4_nilthread;
        //page_table[i].pr_next = PTE_SENTINEL;
	page_table[i].referenced = 0;
	page_table[i].dirty = 0;
	page_table[i].being_updated = 0;
	page_table[i].error_in_transfer = 0;
        page_table[i].pinned = 0;
    }
    
    for(int i=0;i<MAX_SWAP_ENTRIES;i++) {
        swap_table[i].tid = L4_nilthread;
	swap_table[i].offset = PTE_SENTINEL;
	//swap_table[i].chain = PTE_SENTINEL;
	//swap_table[i].size = 0;
	swap_table[i].next_free = i+1;
    }
    //head_of_table = tail_of_table = PTE_SENTINEL;
    /*if(!swapInitialised) {
      initialise_swap();
      }*/
    // add a guard page against stack overflows
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

void
unmap_all()
{
    for (int i = 0; i < numPTE; i++)
    {
        if (L4_ThreadNo(page_table[i].tid) != L4_ThreadNo(L4_nilthread))
        {
            L4_Fpage_t toUnmap = page_table[i].pageNo;
	    //frame_free(new_low + i*PAGESIZE);
            L4_UnmapFpage(page_table[i].tid, toUnmap);
            //printf("unmapping page %d (%lx, %d)\n", i, L4_ThreadNo(page_table[i].tid), toUnmap.X.b);
        }
    }
    //L4_CacheFlushAll();
}

void initialise_swap_callback(uintptr_t token,int status,struct cookie *fh, fattr_t *attr) {
  //Now we need to call nfs
  int create_issued = 0;
  if(status != 0) {
    printf("Swapfile does not exist!!\n");
    sattr_t attributes;
    attributes.size = 0;
    attributes.mode = 0x06;
    nfs_create(&mnt_point,SWAPFILE,&attributes,initialise_swap_callback,token);
    create_issued = 1;
  }
  if(create_issued || status == 0) {
    printf("\nSwapfile created !!\n");
    printf("Setting the cookie");
    //swap_file_size = attr->size;
    printf("The size of swap file is ... %d\n",swap_file_size);
    swapcookie = (struct cookie *)malloc(sizeof(struct cookie));
    swapInitialised = 1;
    memcpy(swapcookie,fh,sizeof(struct cookie));
  }
}

void pager_write_callback(uintptr_t token, int status, fattr_t *attr) {
  //printf("Write callback invoked status %d!\n",status);
  struct page_token *token_val = (struct page_token *) token;
  int pageIndex = token_val -> pageIndex;
  int swapIndex = token_val -> swapIndex;

  if(status != 0) {
    //Something bad happened so we cannot overwrite the page content, lets delay for 
    //next page fault
    page_table[pageIndex].error_in_transfer = 1;
  }

  page_table[pageIndex].write_bytes_transferred += NFS_WRITE_SIZE;
  //printf("Write callback bytes_transferred %d",page_table[pageIndex].write_bytes_transferred);

  //Remember to think of the case where write succeeds but read fails

  if(page_table[pageIndex].write_bytes_transferred == PAGESIZE) {
    //printf("Finished assembling bytes %d \n",token_val -> send_reply);
    if(!page_table[pageIndex].error_in_transfer) {
        //Now we need to move things around in the table
	swap_table[swapIndex].tid = token_val -> source_tid;
	swap_table[swapIndex].pageNo = token_val -> source_page;
	//Unmap the fpage which got written out
	if(token_val -> send_reply) {
	  L4_PhysDesc_t phys = L4_PhysDesc(new_low + pageIndex * PAGESIZE, L4_DefaultMemory);
          L4_Set_Rights(&(token_val -> destination_page),L4_Readable);  
	  L4_MapFpage(token_val -> destination_tid, token_val -> destination_page,phys);
 	  L4_CacheFlushAll();
	  page_table[pageIndex].tid = token_val -> destination_tid;
	  page_table[pageIndex].pageNo = token_val -> destination_page;
	  page_table[pageIndex].referenced = 1;
	  page_table[pageIndex].dirty = 0;
	} else {
	  //There is a read coming up
	  //page_table[pageIndex].tid = L4_nilthread;
          readFromSwap(token_val -> swapIndexToBeReadIn,pageIndex,1,token_val -> destination_tid,token_val -> destination_page);
	}
	//If there is a read after this it will copy the page_table contents
	
    } else {
      // If there is an error in transfer, currently we have nothing to do
      //Since this write was invalidated we undo the change
      swap_file_size -= PAGESIZE;
      //Undo the swap that was allocated
      swap_table[swapIndex].next_free = head_free_swap;
      head_free_swap = swapIndex;
    }
    if(token_val -> send_reply || page_table[pageIndex].error_in_transfer) {
      //We clear the error in transfer value and being updated since the work is done

      page_table[pageIndex].being_updated = page_table[pageIndex].error_in_transfer = 0;
      //Now send the reply message
      printf("Sending message after write callback\n");
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

void initialise_swap() {
  nfs_lookup(&mnt_point,SWAPFILE,initialise_swap_callback,L4_Myself().raw);
}

void pager_read_callback(uintptr_t token,int status, fattr_t *attr, int bytes_read,char *data) {
  struct page_token *token_val = (struct page_token *) token;
  int pagetableIndex = token_val -> pageIndex;
  int byte_index = token_val -> chunk_index;
  int swapIndex = token_val -> swapIndex;
  if(status != 0) {
    page_table[pagetableIndex].error_in_transfer = 1;
  } else {
    char *memstart = (char *) (new_low + pagetableIndex * PAGESIZE + byte_index*NFS_READ_SIZE); 
    memcpy(memstart,data,NFS_READ_SIZE);
  }
    
  page_table[pagetableIndex].read_bytes_transferred += NFS_READ_SIZE;

  if(page_table[pagetableIndex].read_bytes_transferred == PAGESIZE) {
    if(page_table[pagetableIndex].error_in_transfer == 1) {
      //The memory in pagetable is inconsistent so the best thing would be to mark the page table entry as
      //unreferenced and hopefully its evicted 
      //If this occurs for a free frame its best to free the frame
      if(!token_val -> writeToSwapIssued) {
        frame_free(new_low + pagetableIndex * PAGESIZE);
      }
      //Unmap the page table page whose memory we corrupted
      L4_UnmapFpage(page_table[pagetableIndex].tid,page_table[pagetableIndex].pageNo);
      L4_CacheFlushAll();
      page_table[pagetableIndex].tid = L4_nilthread;
      page_table[pagetableIndex].referenced = 0;
      page_table[pagetableIndex].dirty = 0;
    } else {
      swap_table[swapIndex].tid = L4_nilthread;
      swap_table[swapIndex].next_free = head_free_swap;
      head_free_swap = swapIndex;
      page_table[pagetableIndex].tid = token_val -> destination_tid;
      page_table[pagetableIndex].pageNo = token_val -> destination_page;
      page_table[pagetableIndex].referenced = 1;
      page_table[pagetableIndex].dirty = 0;
      if(token_val -> writeToSwapIssued) {
        L4_UnmapFpage(token_val -> source_tid,token_val -> source_page);
      }
      L4_Set_Rights(&(token_val -> destination_page),L4_Readable);  
      L4_PhysDesc_t phys = L4_PhysDesc(new_low + pagetableIndex * PAGESIZE, L4_DefaultMemory);
      L4_MapFpage(token_val -> destination_tid, token_val -> destination_page, phys);
      L4_CacheFlushAll();
      //Everything went fine, its the Eureka moment
    }
    printf("Sending message after read callback\n");
    L4_Msg_t msg;
    L4_MsgClear(&msg);
    L4_MsgLoad(&msg);
    L4_Reply(token_val -> destination_tid);
    update_process_table_size(token_val -> destination_tid,1);
    page_table[pagetableIndex].being_updated = page_table[pagetableIndex].error_in_transfer = 0;
  }
  free(token_val);
}

void unmap_process(L4_ThreadId_t tid_killed) {
  for(int i=0;i<numPTE;i++) {
    if(L4_ThreadNo(page_table[i].tid) == L4_ThreadNo(tid_killed)) {
      
        page_table[i].tid = L4_nilthread;
	page_table[i].referenced = 0;
	page_table[i].dirty = 0;
	page_table[i].being_updated = 0;
	page_table[i].error_in_transfer = 0;
        page_table[i].pinned = 0;
    }
  }
  
  for(int i=0;i<MAX_SWAP_ENTRIES;i++) {
    if(L4_ThreadNo(swap_table[i].tid) == L4_ThreadNo(tid_killed)) {
      
        swap_table[i].tid = L4_nilthread;
	swap_table[i].next_free = head_free_swap;
	head_free_swap = i;
    }
  }
  L4_CacheFlushAll();
}

int
pager(L4_ThreadId_t tid, L4_Msg_t *msgP)
{
    send = 1;
    // Get the faulting address
    L4_Word_t addr = L4_MsgWord(msgP, 0);
    L4_Word_t physicalAddress;
    L4_Word_t permission;
    L4_MsgTag_t tag;
    // Alignment
    printf("address location: %lx ",addr);
    addr = (addr / PAGESIZE)*PAGESIZE;
    tag = L4_MsgMsgTag(msgP);
    L4_Word_t access_type = L4_Label(tag) & 0x07;

    printf("pager invoked addr=%lx by %lx for access 0x%lx\n", addr,L4_ThreadNo(tid),access_type);

    // Construct fpage IPC message
    L4_Fpage_t targetFpage = L4_FpageLog2(addr, 12);
    
    if(VIRTUAL(addr)) 
    {

    	int inPage = isInPage(tid, targetFpage);
    	if (inPage == -1)
    	{
	  //We need to check if the page is in swap
	  printf("debug 1 .. ");
	    inPage = isInSwap(tid,targetFpage);
	    printf("debug 2 .. ");
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
    	//All virtual addresses are fully accessible
	    printf("physical address is %lx\n", physicalAddress);
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
    //printf("mapped to addr=%lx\n", physicalAddress);
    L4_PhysDesc_t phys = L4_PhysDesc(physicalAddress, L4_DefaultMemory);

    if ( !L4_MapFpage(tid, targetFpage, phys) ) {
        sos_print_error(L4_ErrorCode());
        printf(" Can't map page at %lx\n", addr);
    }
    //printf("Should be here");
    L4_CacheFlushAll();
    return send;
}

