/****************************************************************************
 *
 *      $Id: frames.c,v 1.3 2003/08/06 22:52:04 benjl Exp $
 *
 *      Description: Example frame table implementation
 *
 *      Author:      Ben Leslie
 *
 ****************************************************************************/

#include <stddef.h>
#include "libsos.h"
#include <stdio.h>
#include "frames.h"

typedef union {L4_Word_t val; L4_Word_t* ptr;} sos_address_t;
static L4_Word_t low_boundary = 0;
static sos_address_t lastFree;

/*
  Initialise the frame table. The current implementation is
  clearly not sufficient.
*/
void
frame_init(L4_Word_t low, L4_Word_t high)
{
    sos_address_t prev, current;
    // if we arrive at low, the memory is full
    prev.val = low;
    *(prev.ptr) = 0;
    // link the entire memory
    for (prev.val = low, current.val = prev.val+PAGESIZE; 
	    current.val < high;
	    prev.val+=PAGESIZE, current.val+=PAGESIZE)
    {
	    *(current.ptr) = prev.val;
    }
    // init lastFree
    lastFree = prev;
}

/*
 * Set the new_low value a frame below new_low is not allocated
 * This is because the page table is stored from low -> new_low
 */
void
set_new_low(L4_Word_t new_low)
{
    low_boundary = new_low;
}

/*
  Allocate a currently unused frame 
*/
L4_Word_t
frame_alloc(void)
{
    // take the last free frame
    sos_address_t frame = lastFree;
    // set the last free frame to the one before that
    if (frame.val > low_boundary)  {
      lastFree.val = *(lastFree.ptr);
      return frame.val;
    } else 
      return 0;
}

/*
  Add a frame to the free frame list
*/
void
frame_free(L4_Word_t frame)
{
    // take the incoming frame as the last free frame
    sos_address_t newLastFree;
    newLastFree.val = frame;
    // link it to the old last free frame
    *(newLastFree.ptr) = lastFree.val;
    lastFree.val = newLastFree.val;
}

