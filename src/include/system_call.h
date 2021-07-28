#ifndef __SYSTEM_CALL_H_
#define __SYSTEN_CALL_H_

#include "printk.h"
#include "ptrace.h"

#define MAX_SYSTEM_CALL_NR 128
typedef unsigned long (*system_call_t)(struct pt_regs *regs);
unsigned long no_system_call(struct pt_regs *regs) {
  color_printk(RED, BLACK, "no_system_call is calling, NR:%#04x\n", regs->rax);
  return -1;
}

unsigned long sys_printf(struct pt_regs *regs) {
  color_printk(BLACK, WHITE, (char *)regs->rdi);
  return 1;
}

system_call_t system_call_table[MAX_SYSTEM_CALL_NR] = { 
  [0] = no_system_call,
  [1] = sys_printf,
  [2 ... MAX_SYSTEM_CALL_NR - 1] = no_system_call
};

#endif