#include "task.h"
#include "gate.h"
#include "lib.h"
#include "linkage.h"
#include "mem.h"
#include "printk.h"
#include "ptrace.h"

extern void ret_from_intr(void);
extern void ret_system_call(void);
extern void system_call(void);

/**
 * @brief 根据 regs 中保存的系统调用号，分发系统调用处理函数
 * 
 * @param regs 上下文参数
 */
unsigned long system_call_function(struct pt_regs *regs) {
	return system_call_table[regs->rax](regs);
}

/**
 * @brief 执行系统调用的过程
 * 1. 用户层将执行完系统调用的返回地址保存到 RDX，将执行完系统调用的栈地址保存到 RCX
 * 2. 指定系统调用号
 * 3. 执行 SYSENTER 执行系统调用
 * 4.1 SYSENTER 指令会从 IA32_SYSENTER_EIP 上取得系统调用的入口地址
 * 4.2 SYSENTER 指令从 IA32_ENTER_ESP 上取得系统调用使用的 RSP
 * 4.3 SYSENTER 指令从 IA32_ENTER_CS 上取得 SS, CS 段选择子，并设置段描述符
 * 5. 进入系统调用入口地址函数 system_call，该函数会保存通用寄存器
 * 6. 调用 system_call_function，根据系统调用号分配处理函数，然后执行系统调用处理函数
 */
void user_level_function() {
  color_printk(RED, BLACK, "user_level_function task is running\n");
  long ret = 0;

  __asm__ __volatile__("leaq	sysexit_return_address(%%rip),	%%rdx	\n\t"		/* 设置返回地址到 RDX 寄存器，SYSEXIT 指令会把它加载到 RIP 寄存器 */
                       "movq	%%rsp,	%%rcx	\n\t"														/* 设置用户层的栈顶寄存器到 RDX，SYSEXIT 指令会把它加载到 RSP 寄存器 */
                       "sysenter	\n\t"																			/* 执行 SYSENTER 指令，进入内核层 */
                       "sysexit_return_address:	\n\t"												/* 系统调用执行完毕的返回地址 */
                       : "=a"(ret)																					/* 系统调用执行的返回结果保存在 rax 中，并赋值给 ret */
                       : "0"(15)																						/* rax 保存系统调用号 */
                       : "memory");
  color_printk(RED, BLACK, "user_level_function task called sysenter, ret:%ld\n", ret);
  while (1)
    ;
}

/**
 * @brief 手动设置 regs 来构造出新的执行环境
 * 当 do_execve 函数返回的时候会跳转至 ret_system_call，进而把 regs 还原到各个寄存器中
 * 
 * @param regs 
 * @return unsigned long 
 */
unsigned long do_execve(struct pt_regs *regs) {
	/**
	 * 0x800000 的选择只要是一个未使用的内存空间都可以，
	 * 0xa00000 是为了与 0x800000 在同一个物理页内
	 */
	regs->rdx = 0x800000;		/* SYSEXIT 指令会使用 RDX 寄存器的值作为用户层的 RIP */
	regs->rcx = 0xa00000;		/* SYSEXIT 指令会使用 RCX 寄存器的值作为用户层的 RSP */
	regs->rax = 1;					/* 系统调用的返回值 */
	regs->ds = regs->es = 0;
	color_printk(RED, BLACK, "do_execve task is running\n");

	/* 将用户层的执行函数复制到用户层的第一条指令开始地址处 */
	memcpy(user_level_function, (void *)regs->rdx, 1024);
	return 0;
}

/**
 * @brief 进入用户层的步骤
 * 1. 设置好 do_execve 函数的返回地址为 ret_system_call，该函数返回时会从此地址开始执行
 * 2. 设置好执行 ret_system_call 时的栈顶指针为 regs 结构体
 * 3. 进入 do_execve 设置 SYSEXIT 指令使用到的 RDX 和 RCX，将用户层函数的地址拷贝到第一条指令处
 * 4. do_execve 函数返回，执行 ret_system_call
 * 5. ret_system_call 函数会根据 regs 恢复执行现场，最后执行一个 SYSEXIT 指令设置 RIP, RSP, SS, CS
 * 6. 从 RIP 寄存器指定的指令地址，继续执行，此时就开始执行用户层的代码了
 */
unsigned long init(unsigned long arg) {
  struct pt_regs *regs;
  color_printk(RED, BLACK, "init task is running,arg:%#018lx\n", arg);
	
	/* do_execve 的返回地址 */
  current->thread->rip = (unsigned long)ret_system_call;
  current->thread->rsp = (unsigned long)current + STACK_SIZE - sizeof(struct pt_regs);
  regs = (struct pt_regs *)current->thread->rsp;
  __asm__ __volatile__("movq	%1,	%%rsp \n\t"
                       "pushq	%2 \n\t"				/* 将返回地址压入栈中，等待 ret 指令执行 */
                       "jmp	do_execve	\n\t" 	/* do_execve 为新程序准备执行环境，RDI 寄存器存放了 do_execve 的参数即 regs */
											 ::"D"(regs), "m"(current->thread->rsp), "m"(current->thread->rip)
                       : "memory");
  return 1;
}

/**
 * @brief 进程切换的后半部分函数
 * 返回的时候会从的栈上弹出栈顶的指令（在前半部分压栈的 next 的 rip）执行
 * 
 * @param prev 当前正在执行的进程
 * @param next 新进程
 */
void __switch_to(struct task_struct *prev, struct task_struct *next) {
  /**
   * @brief 设置 TSS 的内核栈地址
   * 每个进程在执行的时候 TSS 的 rsp0 都设置为该进程的内核栈
   * 当发生中断的时候如果没有指定 ist 那么直接使用进程的内核栈
   * 
   * 一个 CPU 只有一个 TSS，TR 寄存器永远指向那个地址
   */
  init_tss[0].rsp0 = next->thread->rsp0;
  /* 更新当前 CPU 的 TSS 段 */
  set_tss64(init_tss[0].rsp0, init_tss[0].rsp1, init_tss[0].rsp2,
            init_tss[0].ist1, init_tss[0].ist2, init_tss[0].ist3,
            init_tss[0].ist4, init_tss[0].ist5, init_tss[0].ist6,
            init_tss[0].ist7);
  /* 保存当前进程的 fs, gs 数据段寄存器 */
  __asm__ __volatile__("movq %%fs, %0 \n\t" : "=a"(prev->thread->fs));
  __asm__ __volatile__("movq %%gs, %0 \n\t" : "=a"(prev->thread->gs));

  /* 设置 fs, gs 数据段寄存器为 next 进程的上下文 */
  __asm__ __volatile__("movq %0, %%fs \n\t" :: "a"(next->thread->fs));
  __asm__ __volatile__("movq %0, %%gs \n\t" :: "a"(next->thread->gs));

  color_printk(WHITE, BLACK, "prev->thread->rsp0:%#018lx\n", prev->thread->rsp0);
  color_printk(WHITE, BLACK, "next->thread->rsp0:%#018lx\n", next->thread->rsp0);
}

/**
 * @brief task_struct 释放函数
 * 
 * @param code 进程执行的返回值
 * @return unsigned long 
 */
unsigned long do_exit(unsigned long code) {
  color_printk(RED, BLACK, "exit task is running, arg:%#018lx\n", code);
  while(1);
}

/**
 * 对于内核线程
 * 1. 进程切换的时候首先会执行 switch_to 函数
 * 2. 然后 switch_to 函数会从栈上弹出内核线程的 RIP，RIP 指向了 kernel_thread_func 函数
 * 3. kernel_thread_func() 会把线程的执行现场恢复，然后跳转到由 rbx 寄存器保存的一个函数执行
 * 4. 执行完毕后调用 do_exit 结束
 * 
 * 对于用户线程
 * 1. 进程切换的时候首先会执行 switch_to 函数
 * 2. 然后 switch_to 函数会从栈上弹出内核线程 thread_struct 保存的 RIP，RIP 指向了 ret_from_intr 函数
 * 3. ret_from_intr 函数会弹出一些结构体，但是没有向内核线程那样的弹出彻底，最后会停留在栈上的 RIP 值，这个 RIP 是在指定 thread_struct->rip 之前设置的 RIP
 * 4. iret 返回，执行 RIP 对应的指令
 */
void kernel_thread_func(void);
__asm__("kernel_thread_func:  \n\t"
        "popq %r15  \n\t"
        "popq %r14  \n\t"
        "popq %r13  \n\t"
        "popq %r12  \n\t"
        "popq %r11  \n\t"
        "popq %r10  \n\t"
        "popq %r9   \n\t"
        "popq %r8   \n\t"
        "popq %rbx  \n\t"
        "popq %rcx  \n\t"
        "popq %rdx  \n\t"
        "popq %rsi  \n\t"
        "popq %rdi  \n\t"
        "popq %rbp  \n\t"
        "popq %rax  \n\t"
        "movq %rax, %ds \n\t"
        "popq %rax  \n\t"
        "movq %rax, %es \n\t"
        "popq %rax  \n\t"
        "addq $0x38,  %rsp \n\t"	/* 0x38 = 56 直接跳过了栈中 pt_regs 从 func 开始的最后 7 个数据 */
        "movq %rdx, %rdi  \n\t"
        "callq  *%rbx \n\t"
        "movq %rax, %rdi  \n\t" 	/* call *rbx 的返回值保存到 rdi 作为下一个函数的参数 */
        "callq  do_exit \n\t");

/* 初始化进程的 task_struct 和 thread_struct 结构体 */
unsigned long do_fork(struct pt_regs *regs, unsigned long clone_flags,
                      unsigned long stack_start, unsigned long stack_size) {
  struct task_struct *tsk = NULL;   /* 进程描述符 */
  struct thread_struct *thd = NULL; /* 进程执行现场 */
  struct page *p = NULL;

  /* 首先分配一页内存，用来保存 tsk 和 thd */
  color_printk(WHITE, BLACK, "alloc_pages,bitmap:%#018lx\n", *memory_management_struct.bits_map);
  p = alloc_pages(ZONE_NORMAL, 1, PG_PTable_Maped | PG_Active | PG_Kernel);
  color_printk(WHITE, BLACK, "alloc_pages,bitmap:%#018lx\n", *memory_management_struct.bits_map);

  /**
   * @brief 下面开始初始化 task_struct 以及 thread_struct
   * 首先初始化 task_struct
   */
  tsk = (struct task_struct *)phy_to_virt(p->PHY_address);  /* tsk 保存在分配得到的物理页起始位置 */
  color_printk(WHITE, BLACK, "struct task_struct address:%#018lx\n", (unsigned long)tsk);
  memset(tsk, 0, sizeof(*tsk)); /* 清空 tsk */
  *tsk = *current;              /* copy 0 号进程的描述符 */
  list_init(&tsk->list);        /* 初始化 tsk 的列表 */
  list_add_to_before(&init_task_union.task.list, &tsk->list); /* 加入进程描述符队列的尾部 */
  tsk->pid++;                   /* 设置进程 ID */

  /**
   * thread_struct 紧接着 task_struct
   */
  thd = (struct thread_struct *)(tsk + 1);
  tsk->thread = thd;
  
  /* 伪造进程执行现场，将执行现场数据复制到目标进程内核栈顶，这样在恢复现场的时候就可以弹出了 */
  memcpy(regs, (void *)((unsigned long)tsk + STACK_SIZE - sizeof(struct pt_regs)), sizeof(struct pt_regs));
  thd->rsp0 = (unsigned long)tsk + STACK_SIZE;  /* 设置进程栈 */
  thd->rip = regs->rip;                         /* 设置进程被调度的时候执行的指令 */
  thd->rsp = (unsigned long)tsk + STACK_SIZE - sizeof(struct pt_regs);  /* 设置当前栈顶指针 */

  /* 如果不是内核线程，将进程的第一条指令更改为 ret_from_intr，直接从栈上恢复现场 */
  if(!(tsk->flags & PF_KTHREAD))
    thd->rip = regs->rip = (unsigned long)ret_system_call;  /* 这里更改 RIP 并不会影响已经保存在栈上的寄存器值(memcpy 那一行) */

  tsk->state = TASK_RUNNING;  /* 设置进程的状态 */
  return 0;
}

/* 进程创建函数，为新进程准备执行现场，regs 寄存器内容 */
int kernel_thread(unsigned long (*fn)(unsigned long), unsigned long arg, unsigned long flags) {
  struct pt_regs regs;
  memset(&regs, 0, sizeof(regs));
  regs.rbx = (unsigned long)fn;   /* 程序入口地址 */
  regs.rdx = (unsigned long)arg;  /* 进程创建者传入的参数 */
  regs.ds = KERNEL_DS;
  regs.es = KERNEL_DS;
  regs.cs = KERNEL_CS;
  regs.ss = KERNEL_DS;
  regs.rflags = (1 << 9);
  regs.rip = (unsigned long)kernel_thread_func; /* 引导程序 */
  return do_fork(&regs, flags, 0, 0);
}

void task_init() {
  struct task_struct *p = NULL;

  /**
   * @brief 0 号进程不存在用户层空间
   * 它的 mm_struct 保存的不是应用程序的信息，而是内核程序的各个段信息以及内核层的段基地址
   */
  init_mm.pgd = (pml4t_t *)Global_CR3;
  init_mm.start_code = memory_management_struct.start_code;
  init_mm.end_code = memory_management_struct.end_code;
  init_mm.start_data = (unsigned long)&_data;
  init_mm.end_data = memory_management_struct.end_data;
  init_mm.start_rodata = (unsigned long)&_rodata;
  init_mm.end_rodata = (unsigned long)&_erodata;
  init_mm.start_brk = 0;
  init_mm.end_brk = memory_management_struct.end_brk;
  init_mm.start_stack = _stack_start;

	/* 设置 IA32_SYSENTER_CS 寄存器，SYSENTER 和 SYSEXIT 指令都会用到 */
	wrmsr(0x174, KERNEL_CS);
	/* IA32_SYSENTER_ESP，SYSENTER 指令会将该寄存器里面的值载入到 RSP 寄存器中 */
	wrmsr(0x175, current->thread->rsp0);
	/* IA32_ENTER_EIP，SYSENTER 指令会将该寄存器的值载入到 RIP 寄存器中 */
	wrmsr(0x176, (unsigned long)system_call);

  /* 初始化内存中的 TSS */
  set_tss64(init_thread.rsp0, init_tss[0].rsp1, init_tss[0].rsp2,
            init_tss[0].ist1, init_tss[0].ist2, init_tss[0].ist3,
            init_tss[0].ist4, init_tss[0].ist5, init_tss[0].ist6,
            init_tss[0].ist7);
  init_tss[0].rsp0 = init_thread.rsp0;

  /* 在创建系统第一个 task_struct 的时候没有初始化 list，这里初始化一下 */
  list_init(&init_task_union.task.list);

  kernel_thread(init, 10, CLONE_FS | CLONE_FILES | CLONE_SIGNAL);
  init_task_union.task.state = TASK_RUNNING;  /* 设置当前进程状态 */
  p = container_of(list_next(&current->list), struct task_struct, list);  /* 获取下一个进程准备切换执行 */
  switch_to(current, p);
}