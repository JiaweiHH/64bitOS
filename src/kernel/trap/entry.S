#include "linkage.h"

/* 下面这些符号定义了进程现场保存完毕时，栈中各寄存器相对于栈顶的地址 */
R15	=	0x00
R14	=	0x08
R13	=	0x10
R12	=	0x18
R11	=	0x20
R10	=	0x28
R9	=	0x30
R8	=	0x38
RBX	=	0x40
RCX	=	0x48
RDX	=	0x50
RSI	=	0x58
RDI	=	0x60
RBP	=	0x68
DS	=	0x70
ES	=	0x78
RAX	=	0x80
FUNC	=	0x88
/* 必须根据异常的实际功能才可以确定是否有错误码入栈 */
ERRCODE	=	0x90

/* 下面这些寄存器无需我们手动保存 */

/* 无特权切换时会使用 */
RIP	=	0x98
CS	=	0xa0
RFLAGS	=	0xa8  /* EFLAGS */
/* 有特权切换时会额外使用 */
OLDRSP	=	0xb0  /* RSP */
OLDSS	=	0xb8    /* SS */

/* 恢复寄存器 */
RESTORE_ALL:
  popq  %r15
  popq  %r14
  popq  %r13
  popq  %r12;		
	popq  %r11;		
	popq  %r10;		
	popq  %r9;
	popq  %r8;
	popq  %rbx;
	popq  %rcx;
	popq  %rdx;
	popq  %rsi;
	popq  %rdi;
	popq  %rbp;
  /* push CS/DS/ES/SS 和 pop 是无效的 */
	popq  %rax;
  movq  %rax,   %ds
  popq  %rax
  movq  %rax,   %es
  popq  %rax
  /* 弹出 FUNC 和 ERRCODE */
  addq  $0x10,  %rsp
  /**
   * IRET 指令只会还原之前保存的 EFLAGS 寄存器值，
   * 并且如果发生栈切换还会就将 OLDSS、OLDRSP 从栈中弹出，切换回被中断的程序栈
   * EFLAGS、SS、RSP 由处理器自行入栈
   */
  iretq

ret_from_exception:
ENTRY(ret_from_intr)
  jmp   RESTORE_ALL


error_code:
  /* 然后继续参照上面描述符的寄存器顺序，反向将各寄存器压入栈中 */
  pushq %rax
	movq  %es,	%rax
	pushq %rax
	movq  %ds,	%rax
	pushq %rax
	xorq  %rax,	%rax

	pushq %rbp
	pushq %rdi
	pushq %rsi
	pushq %rdx
	pushq %rcx
	pushq %rbx
	pushq %r8
	pushq %r9
	pushq %r10
	pushq %r11
	pushq %r12
	pushq %r13
	pushq %r14
	pushq %r15

  cld

  /* 函数调用时候参数从左至右使用的寄存器是 RDI, RSI, RDX, RCX, R8, R9 */
  movq  ERRCODE(%rsp),  %rsi  /* 获取 ERRCODE，给异常处理函数使用 */
  movq  FUNC(%rsp), %rdx      /* 获取 处理函数地址 */
  
  movq  $0x10,  %rdi          /* 出发异常的时候可能运行在 DPL = 3，而异常处理程序运行在 DPL = 0，所以需要重新设置数据段为内核数据段 */
  movq  %rdi,   %ds
  movq  %rdi,   %es
  
  movq  %rsp,   %rdi          /* 存入栈指针，给异常处理函数使用 */
  callq *%rdx                 /* 调用异常处理函数，带 * 表示绝对地址调用，否则表示相对地址调用 */
  jmp   ret_from_exception    /* 还原被打断的现场 */

/** 
 * 0 #DE 异常处理模块，该异常不会产生错误码
 * 异常处理的模版也是该 ENTRY 所示的这样
 */
ENTRY(divide_error)
  /* 这里先压入错误码和处理函数地址 */
  pushq $0      /* 为确保一致性，将 0 当作错误码压栈 */
  pushq %rax
  leaq  do_divide_error(%rip),  %rax  /* 异常处理函数地址 */
  xchgq %rax, (%rsp)                  /* 交换栈顶的值和 rax 的值 */
  jmp   error_code

ENTRY(debug)
	pushq	$0
	pushq	%rax
	leaq	do_debug(%rip),	%rax
	xchgq	%rax,	(%rsp)
	jmp	error_code

/* 2 NMI 不可屏蔽中断，不会产生错误码 */
ENTRY(nmi)
  pushq $0
  pushq %rax
  leaq  do_nmi(%rip),  %rax
  xchgq %rax, (%rsp)
  jmp   error_code

ENTRY(int3)
	pushq	$0
	pushq	%rax
	leaq	do_int3(%rip),	%rax
	xchgq	%rax,	(%rsp)
	jmp	error_code

ENTRY(overflow)
	pushq	$0
	pushq	%rax
	leaq	do_overflow(%rip),	%rax
	xchgq	%rax,	(%rsp)
	jmp	error_code

ENTRY(bounds)
	pushq	$0
	pushq	%rax
	leaq	do_bounds(%rip),	%rax
	xchgq	%rax,	(%rsp)
	jmp	error_code

ENTRY(undefined_opcode)
	pushq	$0
	pushq	%rax
	leaq	do_undefined_opcode(%rip),	%rax
	xchgq	%rax,	(%rsp)
	jmp	error_code

ENTRY(dev_not_available)	/* need rewrite */
	pushq	$0
	pushq	%rax
	leaq	do_dev_not_available(%rip),	%rax
	xchgq	%rax,	(%rsp)
	jmp	error_code


ENTRY(double_fault)
	pushq	%rax
	leaq	do_double_fault(%rip),	%rax
	xchgq	%rax,	(%rsp)
	jmp	error_code

ENTRY(coprocessor_segment_overrun)
	pushq	$0
	pushq	%rax
	leaq	do_coprocessor_segment_overrun(%rip),	%rax
	xchgq	%rax,	(%rsp)
	jmp	error_code

/* 10 #TS. 无效的 TSS 段，会产生错误码，因此不需要向栈中压入 0 占位 */
ENTRY(invalid_TSS)
  pushq %rax
  leaq  do_invalid_TSS(%rip), %rax
  xchgq %rax, (%rsp)
  jmp   error_code

ENTRY(segment_not_present)
	pushq	%rax
	leaq	do_segment_not_present(%rip),	%rax
	xchgq	%rax,	(%rsp)
	jmp	error_code

ENTRY(stack_segment_fault)
	pushq	%rax
	leaq	do_stack_segment_fault(%rip),	%rax
	xchgq	%rax,	(%rsp)
	jmp	error_code

ENTRY(general_protection)
	pushq	%rax
	leaq	do_general_protection(%rip),	%rax
	xchgq	%rax,	(%rsp)
	jmp	error_code

/* 14 #PF. 页错误 */
ENTRY(page_fault)
  pushq %rax
  leaq  do_page_fault(%rip),  %rax
  xchgq %rax, (%rsp)
  jmp   error_code

ENTRY(x87_FPU_error)
	pushq	$0
	pushq	%rax
	leaq	do_x87_FPU_error(%rip),	%rax
	xchgq	%rax,	(%rsp)
	jmp	error_code

ENTRY(alignment_check)
	pushq	%rax
	leaq	do_alignment_check(%rip),	%rax
	xchgq	%rax,	(%rsp)
	jmp	error_code

ENTRY(machine_check)
	pushq	$0
	pushq	%rax
	leaq	do_machine_check(%rip),	%rax
	xchgq	%rax,	(%rsp)
	jmp	error_code

ENTRY(SIMD_exception)
	pushq	$0
	pushq	%rax
	leaq	do_SIMD_exception(%rip),	%rax
	xchgq	%rax,	(%rsp)
	jmp	error_code

ENTRY(virtualization_exception)
	pushq	$0
	pushq	%rax
	leaq	do_virtualization_exception(%rip),	%rax
	xchgq	%rax,	(%rsp)
	jmp	error_code

ENTRY(ret_system_call)
  movq  %rax, 0x80(%rsp)  /* 保存系统调用的返回值到栈上对应的位置上 */
  popq  %r15
  popq  %r14
  popq  %r13
  popq  %r12		
	popq  %r11		
	popq  %r10		
	popq  %r9
	popq  %r8
	popq  %rbx
	popq  %rcx
	popq  %rdx
	popq  %rsi
	popq  %rdi
	popq  %rbp
  popq  %rax  /* 这个是 DS */
  movq  %rax, %ds
  popq  %rax  /* 这个是 ES */
  movq  %rax, %es
  popq  %rax  /* 这个才是真正的 rax */
  addq  $0x38,  %rsp  /* 跳过从 func 开始的 56 bytes（对应了 7 个 long long 变量） */
                      /* 对于中断和异常来说只能跳过 0x10 也就是 func 和 errorcode，因为栈上后续的变量会被 cpu 使用拿来恢复 ss, cs, rip, rsp, eflags 等 */
                      /* 而对于系统调用返回指令 SYSEXIT 来说，cs, ss 由 IA32_SYSENTER_CS 提供，RIP 由 RDX 提供，RSP 由 RCX 提供 */
  
  .byte 0x48  /* 0x48 修饰 sysexit 前缀，表示要返回到 64 位模式的应用层 */
  sysexit     /* sysexit 需要借助 RDX 和 RCX 来恢复应用程序的执行线程，这两个寄存器由应用程序在进入内核层之前特殊处理 */