#ifndef __INTERRUPT_H_
#define __INTERRUPT_H_

#include "linkage.h"

void init_interrupt();
void do_IRQ(unsigned long regs, unsigned long nr);

#endif