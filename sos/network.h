#ifndef NETWORK_H
#define NETWORK_H

#include <nfs.h>
#include <l4/types.h>

// Always call network_irq if an interrupt occurs that you are not interested in
extern void network_irq(L4_ThreadId_t *tP, int *sendP);
extern void network_init(void);
extern struct cookie mnt_point;

#endif
