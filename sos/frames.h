/*
 * Written by Vivek Shah
 */
#include <l4/types.h>

void frame_init(L4_Word_t low, L4_Word_t high);
L4_Word_t frame_alloc(void);
void frame_free(L4_Word_t frame);
void set_new_low(L4_Word_t new_low);
