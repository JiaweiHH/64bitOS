/**
 * head.S 主要内容是
 * 1. 创建新的 GDT 和 IDT，并设置相关寄存器
 * 2. 创建新的页目录，设置相关寄存器
 * 3. 跳转到内核主程序执行
 *
 * 1111 1111 1111 1111 1     000 0000 00     00 0000 000     0 0000 000     10000 0000 0000 0000 0000
 * head.S 第一条指令地址在链接脚本中指定，地址是 0xffff 8000 0010 0000
 * 这个时候 cr3 是在 loader.asm 中设置的，采用的是前面设置的页表
 * 0xffff 8000 0010 0000 对应的 PML4 索引是 0x0，PDPT 索引是 0x0，PDT 索引是 0x0，页内偏移是 1MB
 * 最终 0xffff8... 被映射到物理地址 0x10 0000
 */

.section .text
/* start */
.global _start

_start:
  /* 初始化段寄存器，栈指针 */
  mov   $0x10,  %ax
  mov   %ax,    %ds
  mov   %ax,    %es
  mov   %ax,    %fs
  mov   %ax,    %ss
  mov   $0x7E00,  %esp
  
  /* 加载 GDTR */
  lgdt  GDT_POINTER(%rip)
  
  /* 加载 IDTR */
  lidt  IDT_POINTER(%rip)
  mov   $0x10,  %ax
  mov   %ax,    %ds
  mov   %ax,    %es
  mov   %ax,    %fs
  mov   %ax,    %gs
  mov   %ax,    %ss
  mov   $0x7E00,  %rsp

  /* 设置 CR3 */
  /**
   * __PML4E 在本程序内的虚拟地址是 0xffff 8000 0010 0000 + 0x1000，
   * 最终映射到的物理地址是 0x101000，所以把 0x101000 赋值给 CR3
   */
  movq  $0x101000,  %rax
  movq  %rax,   %cr3
  
  /**
   * 使用远跳转更新 cs 段寄存器
   * 由于 GAS 编译器暂不支持直接远跳转 JMP/CALL 指令，所以这里采用 lretq 来模拟 ljmp
   */
  movq  switch_seg(%rip), %rax  /* rax 保存 switch_seg 地址的内容 */
  pushq $0x08                   /* 保存段选择子 */
  pushq %rax                    /* 保存函数调用时的返回地址 */
  lretq                         /* 远跳转 弹出段选择子和地址 进行跳转，即 ljmp 0x08:rax */

  /* 64-bit 模式代码 */
switch_seg:
  .quad   entry64
entry64:
  movq  $0x10,  %rax
  movq  %rax,   %ds
  movq  %rax,   %es
  movq  %rax,   %gs
  movq  %rax,   %ss
  movq  $0xffff800000007e00,  %rsp  /* 栈指针 */

/**
 * 初始化 IDT
 * 1. 设置 ignore_int 的中断描述符
 * 2. 将整个 IDT 的 256 个中断描述符都设置为上述描述符
 */
setup_IDT:	/* 初始化 ignore_int 中断描述符 */
  leaq  ignore_int(%rip), %rdx      /* 保存 ignore_int 到 rdx */
  movq  $(0x08 << 16),    %rax
  movw  %dx,  %ax                   /* 现在 rax 31~16bit 保存的是段选择子， 15~0bit 保存的是 ignore_int 地址的低 16bit */
  movq  $(0x8E00 << 32),  %rcx
  addq  %rcx, %rax                  /* 现在 rax 47~32bit 保存的是 0x8E00 标志位 */
  movl  %edx, %ecx                  /* ecx 保存 ignore_int 地址的低 32bit */
  shrl  $16,  %ecx                  /* ecx 保存 ignore_int 地址的 31~16bit */
  shlq  $48,  %rcx                  /* ecx 的 63~48bit 保存 ignore_int 地址的 32~16bit */
  addq  %rcx, %rax                  /* 现在 rax 63~48bit 保存 ignore 地址的 32~16bit */
  shrq  $32,  %rdx                  /* rdx 保存 ignore_int 地址的高 32bit */
  leaq  IDT_Table(%rip),  %rdi      /* IDT_TABLE 地址保存到 rdi */
  mov   $256, %rcx                  /* 下面的循环次数，256 也就是 IDT entry 的个数 */
rp_sidt:		/* 设置 256 个中断描述符，每个中断描述符 16B */
  movq  %rax,   (%rdi)              /* 第 i 此循环，设置第 i 个表项的低 8byte */
  movq  %rdx,   8(%rdi)             /* 设置第 i 个表项的高 8byte */
  addq  $0x10,  %rdi                /* rdi 指向下一个 entry */
  dec   %rcx                        /* 循环次数减一 */
  jne   rp_sidt                     /* 如果 rcx != 0 继续循环 */

set_TSS64:
  /**
   * 这里主要是两个任务
   * 1. 人工设置好 TSS 段描述符，然后写入 GDT，TSS 段描述符占 16B
   * 2. 使用 ltr 指令从 GDT 加载该描述符到 TR 寄存器
   */
  leaq  TSS64_Table(%rip),  %rdx    /* 保存 TSS64_Table 地址到 rdx */
  xorq  %rax,   %rax                /* 清空 rax */
  xorq  %rcx,   %rcx                /* 清空 rcx */
  movq  $0x89,  %rax                /* rax = 0x89 */
  shlq  $40,    %rax                /* rax = 0x0000 8900 0000 0000 */
  movl  %edx,   %ecx                /* ecx 保存 TSS64_Table 地址的低 32bit */
  shrl  $24,    %ecx                /* ecx 7~0bit 保存 TSS64_Table 的 31~24bit */
  shlq  $56,    %rcx                /* rcx 63~56bit 保存 TSS64_Table 的 31~24bit */
  addq  %rcx,   %rax                /* rax = 0x??00 8900 0000 0000，? 代表的是 TSS64_Table 的 31~24bit */
  xorq  %rcx,   %rcx                /* 清空 rcx */
  movl  %edx,   %ecx                /* ecx 保存 TSS64_Table 地址的低 32bit */
  andl  $0xffffff,  %ecx            /* ecx 保存 TSS64_Table 地址的 23~0bit */
  shlq  $16,    %rcx                /* rcx 39~16bit 保存 TSS64_Table 地址的 23~0bit */
  addq  %rcx,   %rax                /* rax = 0x??00 89?? ???? 0000，? 的意思和上述一样 */
  addq  $103,   %rax                /* 103 = 0x67，rax = 0x??00 89?? ???? 0067 */
  leaq  GDT_Table(%rip),  %rdi      /* rdi 保存 GDT_Table 的首地址 */
  movq  %rax,   64(%rdi)            /* GDT_Table 的第 8 个 entry 保存 rax 的值，实际上 rax 的值就是 TSS 段描述符 */
  shrq  $32,    %rdx                /* rdx 保留 TSS_Table 的高 32bit */
  movq  %rdx,   72(%rdi)            /* 第 9 个 entry 保存 TSS_Table 地址的高 32bit */

  mov   $0x40,  %ax                 /* ax 变成 TSS 段选择子，ax = 0000 0000 0100 0000，第 4bit 开始是索引号，刚好等于 8 */
  ltr   %ax                         /* 加载 TSS 描述符到 TR 寄存器 */

  /* 这里同样是利用 lretq 模拟 ljmp */
  movq  go_to_kernel(%rip),   %rax
  pushq $0x08
  pushq %rax
  lretq
go_to_kernel:
  .quad Start_Kernel

/**
 * 默认的 中断/异常 处理函数
 * 1. 保存寄存器
 * 2. 将 ds, es 寄存器设置成内核数据段
 * 3. 准备 color_printk 函数的参数，并调用该函数
 * 4. 提示信息显示后，执行 jmp 指令死循环
 */
ignore_int:
  cld
	/* 保存寄存器 */
  pushq %rax
  pushq %rbx
  pushq %rcx
  pushq %rdx
  pushq %rbp
  pushq %rdi
  pushq %rsi

  pushq %r8
	pushq %r9
	pushq %r10
	pushq %r11
	pushq %r12
	pushq %r13
	pushq %r14
	pushq %r15

  movq  %es,  %rax
  pushq %rax
  movq  %ds,  %rax
  pushq %rax

	/* 设置 ds, es 寄存器为内核数据段 */
  movq  $0x10,  %rax
  movq  %rax,   %ds
  movq  %rax,   %es

	/* 设置参数，调用 color_printk */
  leaq  int_msg(%rip),  %rax
  pushq %rax
  movq  %rax,   %rdx					/* rdx 保存待显示的字符串起始地址 */
  movq  $0x00000000,  %rsi		/* rsi 保存背景颜色 */
  movq  $0x00ff0000,  %rdi		/* rdi 保存字体颜色 */
  movq  $0, %rax
  callq color_printk

	/* rax 出栈 */
  addq  $0x8,   %rsp
Loop:
  jmp   Loop
  popq	%rax
	movq	%rax,	%ds
	popq	%rax
	movq	%rax,	%es

	popq	%r15
	popq	%r14
	popq	%r13
	popq	%r12
	popq	%r11
	popq	%r10
	popq	%r9
	popq	%r8

	popq	%rsi
	popq	%rdi
	popq	%rbp
	popq	%rdx
	popq	%rcx
	popq	%rbx
	popq	%rax
	iretq
int_msg:
	.asciz "Unknow interrupt or fault at RIP\n"		/* asciz 分配一段专门存放字符串的空间，结尾会自动加 '\0' */

/** 
 * 定义 64-bit 页表，每个页表项大小为 8B
 * 对于单个地址来说只有低 48-bit 才能进行页表地址转换
 */
.align  8
.org    0x1000        /* 对应的虚拟地址是 (链接脚本指定的地址 + 0x1000) */
__PML4E:
  .quad   0x102007    /* 线性地址 0 对应的 PML4E */
  .fill   255, 8, 0   /* 填充 0x00-PML4E ~ 0xffff8...-PML4E 中间的 PML4E */
  .quad   0x102007    /* 线性地址 0xffff 8000 0000 0000 对应的 PML4E */
  .fill   255, 8, 0   /* 填充剩余的 PML4E */

.org    0x2000
__PDPTE:
  .quad   0x103003    /* 不管是高地址还是低地址，对应的 PDPTE 索引都是 0 */
  .fill   511, 8, 0   /* 填充剩余的 PDPTE */

/* PDE 的最后 8-bit 中的第 7-bit 都是 1，表示开启了 2MB 的 page size */
.org    0x3000
__PDE:
  /* 0x...0000 在 PDT 中的索引为 0x00 */
  .quad   0x000083    /* 映射前 10MB 的物理地址到 0x00 和 0xffff 8000 0000 0000 */
  .quad   0x200083
  .quad   0x400083
  .quad   0x600083
  .quad   0x800083
  /* 0x...a00000 在 PDT 中的索引为 0x05 */
  .quad   0xe0000083  /* 映射 0xe0000000 开始的 16MB 物理地址到 0xa00000 和 0xffff 8000 00a0 0000 */
                      /* 0xe0000083 物理内存对应的是帧缓存，用来在屏幕上显示颜色，帧缓存的每个存储单元对应一个像素 */
                      /* 这里的帧缓存由于之前在 loader 程序中设置过，每个像素点的颜色深度为 32-bit */
  .quad   0xe0200083
  .quad   0xe0400083
  .quad   0xe0600083
  .quad   0xe0800083
  .quad   0xe0a00083
  .quad   0xe0c00083
  .quad   0xe0e00083
  .fill   499, 8, 0   /* 填充剩余的表项 */


/**
 * 定义 .data 段，
 * 存放 GDT, IDT, TSS, 页表 等数据
 */
.section .data

/* GDT_Table */
/**
 * .global 表示在符号表中标记该符号是一个全局符号
 * 链接器只能处理全局符号
 * 另外 GDT_Table 在这里只表示一个内存地址而已，在 C 中使用则会表示一个变量
 */
.global GDT_Table
GDT_Table:
  /* .quad 表示对于每个数，从当前位置开始分配 8-byte 存放 */
  .quad   0x0000000000000000    /* 0 NULL 描述符 */
  .quad	  0x0020980000000000    /* 1 KERNEL Code 64-bit, Segment 0x08 */
  .quad	  0x0000920000000000		/* 2	KERNEL Data	64-bit, Segment 0x10 */
	.quad	  0x0020f80000000000		/* 3	USER Code 64-bit, Segment 0x18 */
	.quad	  0x0000f20000000000		/* 4	USER Data	64-bit, Segment 0x20 */
	.quad	  0x00cf9a000000ffff		/* 5	KERNEL Code 32-bit, Segment 0x28 */
	.quad	  0x00cf92000000ffff		/* 6	KERNEL Data 32-bit, Segment	0x30 */
  .fill   10,8,0                /* fill repeat, size, value */
GDT_END:

GDT_POINTER:  /* 赋值给 GDTR 寄存器 */
GDT_LIMIT:    .word   GDT_END - GDT_Table - 1
GDT_BASE:     .quad   GDT_Table

/* IDT_Table */
.global IDT_Table

IDT_Table:
  .fill   512, 8, 0
IDT_END:

IDT_POINTER:  /* 赋值给 IDTR 寄存器 */
IDT_LIMIT:    .word   IDT_END - IDT_Table - 1
IDT_BASE:     .quad   IDT_Table

/* TSS64_Table */
.global TSS64_Table

TSS64_Table:
  .fill    13, 8, 0
TSS64_END:

TSS64_POINTER:
TSS64_LIMIT:  .word   TSS64_END - TSS64_Table - 1
TSS64_Base:   .quad   TSS64_Table