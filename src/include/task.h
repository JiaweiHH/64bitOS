#ifndef __TASK_H_
#define __TASK_H_

#include "cpu.h"
#include "lib.h"
#include "mem.h"

/* 由链接脚本给出 */
extern char _text;
extern char _etext;
extern char _data;
extern char _edata;
extern char _rodata;
extern char _erodata;
extern char _bss;
extern char _ebss;
extern char _end;

extern unsigned long _stack_start;  /* 在 head.S 中定义 */
extern void ret_from_intr();

/* 数据段 */
#define KERNEL_CS (0x08)
#define KERNEL_DS (0x10)
#define USER_CS (0x28)
#define USER_DS (0x30)

/* 进程创建标志位 */
#define CLONE_FS	(1 << 0)
#define CLONE_FILES	(1 << 1)
#define CLONE_SIGNAL	(1 << 2)

/* 进程运行状态 */
#define TASK_RUNNING (1 << 0)
#define TASK_INTERRUPTIBLE (1 << 1)
#define TASK_UNINTERRUPTIBLE (1 << 2)
#define TASK_ZOMBIE (1 << 3)
#define TASK_STOPPED (1 << 4)

#define PF_KTHREAD (1 << 0)

/**
 * 进程的内核栈大小为 32768B(32KB)，其中包含了 task_struct 结构体的空间
 * 1. 内核栈低地址处保存 task_struct
 * 2. 内核栈高地址处用作栈空间
 */
#define STACK_SIZE 32768

/* 进程发生调度切换时保存执行现场 */
struct thread_struct {
  unsigned long rsp0;       /* 内核层栈基地址 */
  unsigned long rip;        /* 内核层代码指针 */
  unsigned long rsp;        /* 内核层当前栈指针 */
  unsigned long fs;         /* FS 段寄存器 */
  unsigned long gs;         /* GS 段寄存器 */
  unsigned long cr2;        /* CR2 控制寄存器 */
  unsigned long trap_nr;    /* 产生异常的异常号 */
  unsigned long error_code; /* 异常错误码 */
};

struct mm_struct {
  pml4t_t *pgd;   /* 页目录基地址 */

  unsigned long start_code, end_code;       /* 代码段 */
  unsigned long start_data, end_data;       /* 数据段 */
  unsigned long start_rodata, end_rodata;   /* 只读数据段 */
  unsigned long start_brk, end_brk;         /* 堆 */
  unsigned long start_stack;                /* 栈 */
};

struct task_struct {
  struct List list;       /* 双向链表，用于连接各个进程的控制体 */
  volatile long state;    /* 进程状态 */
  unsigned long flags;    /* 进程标志：进程、线程、内核线程 */

  struct mm_struct *mm;   /* 内存空间分布结构体，记录内存页表和程序段信息 */
  struct thread_struct *thread; /* 进程切换时保留的状态信息 */

  unsigned long addr_limit;     /* 进程地址空间范围 */
                                /* 0x0000000000000000~0x00007fffffffffff 应用层 */
                                /* 0xffff800000000000~0xffffffffffffffff 内核层 */
  long pid;       /* 进程 ID 号 */
  long counter;   /* 进程可用时间片 */
  long signal;    /* 进程持有信号 */
  long priority;  /* 进程优先级 */
};

/**
 * 进程内核栈，32KB 对齐
 * 1. 栈的低地址处保存 task_struct
 * 2. 高地址处也就是栈基地址到 task_struct 之间的空间作为栈使用
 */
union task_union {
  struct task_struct task;
  unsigned long stack[STACK_SIZE / sizeof(unsigned long)];
}__attribute__((aligned(8)));

/* 声明 0 号进程的 mm_struct 和 thread_struct */
struct mm_struct init_mm;
struct thread_struct init_thread;

/* 0 号进程数据结构初始化 */
#define INIT_TASK(tsk)                                                         \
  {                                                                            \
    .state = TASK_UNINTERRUPTIBLE, .flags = PF_KTHREAD, .mm = &init_mm,        \
    .thread = &init_thread, .addr_limit = 0xffff800000000000, .pid = 0,        \
    .counter = 1, .signal = 0, .priority = 0                                   \
  }

/* 定义 0 号进程的栈以及 task_struct 结构体初始化 */
union task_union init_task_union __attribute__((
    __section__(".data.init_task"))) = {INIT_TASK(init_task_union.task)};

struct task_struct *init_task[NR_CPUS] = {&init_task_union.task, 0};  /* 初始化多核 cpu 的 0 号进程 */

/* 这两个数据结构变量的定义 */
struct mm_struct init_mm = {0};
struct thread_struct init_thread = {
  .rsp0 = (unsigned long)(init_task_union.stack + STACK_SIZE / sizeof(unsigned long)),  /* 栈基地址 */
  .rsp = (unsigned long)(init_task_union.stack + STACK_SIZE / sizeof(unsigned long)),   /* 当前栈指针 */
  .fs = KERNEL_DS,
  .gs = KERNEL_DS,
  .cr2 = 0,
  .trap_nr = 0,
  .error_code = 0
};

/**
 * @brief TSS 结构体及其初始化宏
 * 
 */
struct tss_struct {
  unsigned int reserved0;
  unsigned long rsp0;
  unsigned long rsp1;
  unsigned long rsp2;
  unsigned long reserved1;
  unsigned long ist1;
  unsigned long ist2;
  unsigned long ist3;
  unsigned long ist4;
  unsigned long ist5;
  unsigned long ist6;
  unsigned long ist7;
  unsigned long reserved2;
  unsigned short reserved3;
  unsigned short iomapbaseaddr;
}__attribute__((packed));

#define INIT_TSS                                                               \
  {                                                                            \
    .reserved0 = 0,                                                             \
    .rsp0 = (unsigned long)(init_task_union.stack +                             \
                           STACK_SIZE / sizeof(unsigned long)),                \
    .rsp1 = (unsigned long)(init_task_union.stack +                            \
                            STACK_SIZE / sizeof(unsigned long)),               \
    .rsp2 = (unsigned long)(init_task_union.stack +                            \
                            STACK_SIZE / sizeof(unsigned long)),               \
    .reserved1 = 0, .ist1 = 0xffff800000007c00, .ist2 = 0xffff800000007c00,    \
    .ist3 = 0xffff800000007c00, .ist4 = 0xffff800000007c00,                    \
    .ist5 = 0xffff800000007c00, .ist6 = 0xffff800000007c00,                    \
    .ist7 = 0xffff800000007c00, .reserved2 = 0, .reserved3 = 0,                \
    .iomapbaseaddr = 0                                                         \
  }

struct tss_struct init_tss[NR_CPUS] = {[0 ... NR_CPUS - 1] = INIT_TSS}; /* 初始化每个 CPU 的 TSS */

/* 借助当前栈指针，获取进程 task_struct 结构体 */
static inline struct task_struct *get_current() {
  struct task_struct *current = NULL;
  /**
   * 这里输入使用与输出相同的寄存器
   * andq 相当于把准备输出的寄存器与 %%rsp 与运算一下，也就是 %%rsp & ~32767
   * 相当于结果按照 32KB 下取整，刚好 task_struct 结构体保存在栈顶最大值的地方 也就是低地址处
   */
  __asm__ __volatile__("andq %%rsp, %0 \n\t" : "=r"(current) : "0"(~32767UL));
  return current;
}

#define current get_current()

#define GET_CURRENT                                                            \
  "movq %rsp, %rbx \n\t"                                                       \
  "andq $-32768,  %rbx \n\t"

/**
 * @brief 进程切换函数
 * 应用程序在进入内核层的时候已经将通用寄存器保存起来，所以进程切换过程并不涉及保存/还原通用寄存器
 * CS 寄存器也已经在从用户态进入内核态的时候处理器自己切换了
 * 
 * 1. 保存当前进程的 rsp 以及进程返回时候执行的指令 rip
 * 2. 设置 rsp 寄存器为 next 进程的栈，并在栈上保存 next 进程即将执行的指令 rip，
 *    随后会在 __switch_to 函数通过 ret 指令弹出执行
 * 3. RDI 和 RSI 寄存器分别保存 prev 和 next 两个参数地址，作为 __switch_to 函数的参数使用
 * 4. 跳转到 __switch_to 函数执行后续过程
 */
#define switch_to(prev, next)                                                  \
  do {                                                                         \
    __asm__ __volatile__("pushq  %%rbp \n\t"                                   \
                         "pushq  %%rax \n\t"                                   \
                         "movq   %%rsp,  %0 \n\t"                              \
                         "movq   %2, %%rsp \n\t"                               \
                         "leaq   1f(%%rip),  %%rax \n\t"                       \
                         "movq   %%rax,  %1  \n\t"                             \
                         "pushq  %3  \n\t"                                     \
                         "jmp    __switch_to \n\t"                             \
                         "1: \n\t"                                             \
                         "popq   %%rax \n\t"                                   \
                         "popq   %%rbp \n\t"                                   \
                         : "=m"(prev->thread->rsp), "=m"(prev->thread->rip)    \
                         : "m"(next->thread->rsp), "m"(next->thread->rip),     \
                           "D"(prev), "S"(next)                                \
                         : "memory");                                          \
  } while (0)

void task_init();

#endif