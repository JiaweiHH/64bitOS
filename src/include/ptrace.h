#ifndef __PTRACE_H_
#define __PTRACE_H_

/* 系统调用、中断/异常 的执行现场 */
struct pt_regs {
  unsigned long r15;
  unsigned long r14;
  unsigned long r13;
  unsigned long r12;
  unsigned long r11;
  unsigned long r10;
  unsigned long r9;
  unsigned long r8;
  unsigned long rbx;
  unsigned long rcx;
  unsigned long rdx;
  unsigned long rsi;
  unsigned long rdi;
  unsigned long rbp;
  unsigned long ds;
  unsigned long es;
  unsigned long rax;
  unsigned long func;
  unsigned long errcode;
  unsigned long rip;
  unsigned long cs;
  unsigned long rflags;
  unsigned long rsp;
  unsigned long ss;
};

#endif