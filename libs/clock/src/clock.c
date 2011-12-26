#include <stdio.h>
#include <l4/types.h>
#include <l4/map.h>
#include <l4/thread.h>
#include <l4/cache.h>
#include <l4/space.h>
#include <l4/misc.h>
#include <l4/message.h>
#include <l4/ipc.h>

#include "clock.h"
#include "nslu2.h"
#define OSTS_TS (0x00)
#define OSTS_TIM1 (0x04)
#define FULL_CLOCK_TICK_US 64000000
#define SLEEP_QUEUE_SIZE 50
#define OSTS_STATUS_REG (0x20)
#define OSTS_TIMER1_RL_REG (0x10)

static int clock_initialised = 0;
static L4_Fpage_t target_fpage_timestamptimer;
static timestamp_t timestamp_counter = 0;

static sleep_queue_entry_t sleep_queue[SLEEP_QUEUE_SIZE];
static int sleep_queue_entries = 0;

/*
 * Init function to initialise the timer
 */ 
int start_timer(void) {
  if(clock_initialised) {
    return CLOCK_R_OK;
  }
  //Set the interrupt handler
  L4_AssociateInterrupt(L4_GlobalId(NSLU2_TIMESTAMP_IRQ,1),L4_Myself());
  L4_AssociateInterrupt(L4_GlobalId(NSLU2_TIMER1_IRQ,1),L4_Myself());

  //Map the memory page
  L4_PhysDesc_t phy_timestamp_timer = L4_PhysDesc((L4_Word_t)(NSLU2_OSTS_PHYS_BASE + OSTS_TS), L4_UncachedMemory);
  target_fpage_timestamptimer = L4_FpageLog2((L4_Word_t) NSLU2_OSTS_PHYS_BASE,12);
  L4_Set_Rights(&target_fpage_timestamptimer,L4_FullyAccessible);
  if (!L4_MapFpage(L4_Myself(), target_fpage_timestamptimer, phy_timestamp_timer)) {
    return CLOCK_R_UINT;
  } else {
    clock_initialised = 1;
    L4_Word_t address = (L4_Word_t)NSLU2_OSTS_PHYS_BASE + (L4_Word_t)OSTS_TIMER1_RL_REG;
    L4_Word_t *taddress = (L4_Word_t *) address;
    *taddress = 0xfff;

    return CLOCK_R_OK;
  }
}

/*
 * Function invoked by the rpc thread to register a thread in the sleep queue
 */
int register_timer(uint64_t delay, L4_ThreadId_t client) {
  if(!clock_initialised) {
    return CLOCK_R_UINT;
  }
  //We always add an entry at the position of the available sleep queue which is
  //compacted
  timestamp_t min = delay;
  timestamp_t now = time_stamp();
  for(int i=0;i<sleep_queue_entries;i++) {
    if (now - sleep_queue[i].start_time < min) {
      min = now - sleep_queue[i].start_time;
    }
  }

  //So now we know we need the timer0 to interrupt after delay seconds
  if(sleep_queue_entries < SLEEP_QUEUE_SIZE) {
    sleep_queue[sleep_queue_entries].start_time = now;
    sleep_queue[sleep_queue_entries].sleep_time_us = delay;
    sleep_queue[sleep_queue_entries].tid = client;
    sleep_queue_entries++;
    //Now set the sleep time
    L4_Word_t address = (L4_Word_t)NSLU2_OSTS_PHYS_BASE + (L4_Word_t)OSTS_TIMER1_RL_REG;
    L4_Word_t *timer_address = (L4_Word_t *) address;
    *timer_address = (L4_Word_t)(NSLU2_US2TICKS(delay)) | 0x03;
    return CLOCK_R_OK;
  } else {
    return CLOCK_R_FAIL;
  }
}

/*
 * Function which returns the current accumulated value of time since boot
 */
timestamp_t time_stamp(void) {
  if(clock_initialised) {
    L4_Word_t *timer_address = (L4_Word_t *) NSLU2_OSTS_PHYS_BASE;
    return ((timestamp_t)timestamp_counter*64*1000000 + (timestamp_t)NSLU2_TICKS2US((timestamp_t)*timer_address));
  } else {
    return CLOCK_R_UINT;
  }
}

/*
 * Function to stop the timer which clears up the sleep queue
 */
int stop_timer() {
  if(!clock_initialised) {
    return CLOCK_R_UINT;
  }
    L4_Msg_t msg;
    L4_MsgClear(&msg);
    L4_MsgAppendWord(&msg,CLOCK_R_CNCL);
    L4_MsgLoad(&msg);
  //Disassociate the interrupt
  for(int i=0;i<sleep_queue_entries;i++) {
    //Send error message
    L4_Send(sleep_queue[i].tid);
  }
  L4_DeassociateInterrupt(L4_GlobalId(NSLU2_TIMESTAMP_IRQ,1));
  L4_DeassociateInterrupt(L4_GlobalId(NSLU2_TIMER1_IRQ,1));
  if(!L4_UnmapFpage(L4_Myself(),target_fpage_timestamptimer)) {
    return CLOCK_R_FAIL;
  } else {
    return CLOCK_R_OK;
  }
}

/*
 * Function invoked when the timestamp timer interrupt is received to update the time
 */
void handle_timer_timestamp_interrupt(void) {
  timestamp_counter++;
  L4_Word_t address = (L4_Word_t)NSLU2_OSTS_PHYS_BASE + (L4_Word_t)OSTS_STATUS_REG;
  L4_Word_t *timer_address = (L4_Word_t *) address;
  *timer_address = 0x04;
}

/*
 * Function invoker on timer interrupt to handle sleep queue wakeup
 */
void handle_timer_interrupt(void) {
  timestamp_t now = time_stamp();
  timestamp_t min = 9999999999999;
  //We need to find if any of the sleep time is over and then again go to sleep
  for(int i=0;i<sleep_queue_entries;i++) {
    if((now-sleep_queue[i].start_time) >= sleep_queue[i].sleep_time_us) {
      //We need to evict it
      L4_Msg_t msg;
      L4_MsgClear(&msg);
      L4_MsgLoad(&msg);
      L4_Send(sleep_queue[i].tid);
      for(int j=i;j<sleep_queue_entries-1;j++) {
	sleep_queue[j].start_time = sleep_queue[j+1].start_time;
	sleep_queue[j].sleep_time_us = sleep_queue[j+1].sleep_time_us;
	sleep_queue[j].tid = sleep_queue[j+1].tid;
      }
      sleep_queue_entries--;
    } else if(now-sleep_queue[i].start_time < min) {
      min = now - sleep_queue[i].start_time;
    }
  }
  
  L4_Word_t address = (L4_Word_t)NSLU2_OSTS_PHYS_BASE + (L4_Word_t)OSTS_TIMER1_RL_REG;
  L4_Word_t *taddress = (L4_Word_t *) address;
  *taddress = (L4_Word_t)(NSLU2_US2TICKS(min)) | 0x03;

  address = (L4_Word_t)NSLU2_OSTS_PHYS_BASE + (L4_Word_t)OSTS_STATUS_REG;
  volatile L4_Word_t *timer_address = (L4_Word_t *) address;
  *timer_address = 0x02;
}
