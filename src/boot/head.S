/**
 * head.S 主要内容是
 * 1. 创建新的 GDT 和 IDT，并设置相关寄存器
 * 2. 创建新的页目录，设置相关寄存器
 * 3. 跳转到内核主程序执行
 *
 * head.S 第一条指令地址在链接脚本中指定，地址是 0xffff 8000 0010 0000
 * 这个时候 cr3 是在 loader.asm 中设置的，采用的是前面设置的页表
 * 0xffff 8000 0010 0000 对应的 PML4 索引是 0x100，最终 0xffff8... 被映射到物理地址 0x10 0000
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

  /* 这里同样是利用 lretq 模拟 ljmp */
  movq  go_to_kernel(%rip),   %rax
  pushq $0x08
  pushq %rax
  lretq
go_to_kernel:
  .quad Start_Kernel

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

TSS64_POINTER:  /* 赋值给 TSS 的寄存器 */
TSS64_LIMIT:  .word   TSS64_END - TSS64_Table - 1
TSS64_Base:   .quad   TSS64_Table