#include "mem.h"
#include "gate.h"
#include "interrupt.h"
#include "lib.h"
#include "linkage.h"
#include "printk.h"

/* 保存中断的上下文，过程和异常上下文保存大致一样 */
#define SAVE_ALL                                                               \
  "cld;               \n\t"                                                    \
  "pushq  %rax;       \n\t"                                                    \
  "pushq  %rax;       \n\t"                                                    \
  "movq   %es,  %rax; \n\t"                                                    \
  "pushq  %rax;       \n\t"                                                    \
  "movq   %ds,  %rax; \n\t"                                                    \
  "pushq  %rax;       \n\t"                                                    \
  "xorq   %rax, %rax; \n\t"                                                    \
  "pushq  %rbp;       \n\t"                                                    \
  "pushq  %rdi;       \n\t"                                                    \
  "pushq  %rsi;       \n\t"                                                    \
  "pushq  %rdx;       \n\t"                                                    \
  "pushq  %rcx;       \n\t"                                                    \
  "pushq  %rbx;       \n\t"                                                    \
  "pushq  %r8;        \n\t"                                                    \
  "pushq  %r9;        \n\t"                                                    \
  "pushq  %r10;       \n\t"                                                    \
  "pushq  %r11;       \n\t"                                                    \
  "pushq  %r12;       \n\t"                                                    \
  "pushq  %r13;       \n\t"                                                    \
  "pushq  %r14;       \n\t"                                                    \
  "pushq  %r15;       \n\t"                                                    \
  "movq   $0x10, %rdx;\n\t"                                                    \
  "movq   %rdx, %ds;  \n\t"                                                    \
  "movq   %rdx, %es;  \n\t"

#define IRQ_NAME2(nr) nr##_interrupt(void)
#define IRQ_NAME(nr)  IRQ_NAME2(IRQ##nr)

/**
 * 定义中断处理函数的入口部分
 * void IRQ_NAME(nr) 声明了函数 IRQnr_interrupt()
 * 
 * 后续的内嵌汇编定义了 IRQnr_interrupt 的入口部分
 * 1. 调用 SAVE_ALL 保存相关寄存器，以及设置 ds 和 es 为内核代码段
 * 2. 保存 ret_from_intr 的地址，用于中断处理函数执行完毕的 ret 指令
 * 3. 跳转到 do_IRQ 函数执行
 */
#define Build_IRQ(nr)                                                          \
  void IRQ_NAME(nr);                                                           \
  __asm__(SYMBOL_NAME_STR(IRQ) #nr "_interrupt:\n\t"                           \
                                   "pushq $0x00  \n\t" SAVE_ALL                \
                                   "movq %rsp, %rdi  \n\t"                     \
                                   "leaq ret_from_intr(%rip), %rax \n\t"       \
                                   "pushq %rax \n\t"                           \
                                   "movq $" #nr ", %rsi  \n\t"                 \
                                   "jmp do_IRQ \n\t");

Build_IRQ(0x20)
Build_IRQ(0x21)
Build_IRQ(0x22)
Build_IRQ(0x23)
Build_IRQ(0x24)
Build_IRQ(0x25)
Build_IRQ(0x26)
Build_IRQ(0x27)
Build_IRQ(0x28)
Build_IRQ(0x29)
Build_IRQ(0x2a)
Build_IRQ(0x2b)
Build_IRQ(0x2c)
Build_IRQ(0x2d)
Build_IRQ(0x2e)
Build_IRQ(0x2f)
Build_IRQ(0x30)
Build_IRQ(0x31)
Build_IRQ(0x32)
Build_IRQ(0x33)
Build_IRQ(0x34)
Build_IRQ(0x35)
Build_IRQ(0x36)
Build_IRQ(0x37)

/* 定义函数指针数组，数组每个元素都是一个 IRQ 函数 */
void (*interrupt[24])(void) = {
  IRQ0x20_interrupt,
	IRQ0x21_interrupt,
	IRQ0x22_interrupt,
	IRQ0x23_interrupt,
	IRQ0x24_interrupt,
	IRQ0x25_interrupt,
	IRQ0x26_interrupt,
	IRQ0x27_interrupt,
	IRQ0x28_interrupt,
	IRQ0x29_interrupt,
	IRQ0x2a_interrupt,
	IRQ0x2b_interrupt,
	IRQ0x2c_interrupt,
	IRQ0x2d_interrupt,
	IRQ0x2e_interrupt,
	IRQ0x2f_interrupt,
	IRQ0x30_interrupt,
	IRQ0x31_interrupt,
	IRQ0x32_interrupt,
	IRQ0x33_interrupt,
	IRQ0x34_interrupt,
	IRQ0x35_interrupt,
	IRQ0x36_interrupt,
	IRQ0x37_interrupt,
};

/**
 * @brief 中断初始化
 * 1. 初始化中断门描述符
 * 2. 初始化 8259A 中断控制器
 * 3. 开启中断
 */
void init_interrupt() {
  /* 初始化中断门描述符 */
  for(int i = 32; i < 56; ++i) {
    set_intr_gate(i, 2, interrupt[i - 32]);
  }

  color_printk(RED, BLACK, "8259A init \n");

  /**
   * 初始化 8259A-master ICW1-4
   * 1. 主芯片的 ICW1 映射到 0x20 地址
   * 2. 主芯片的 ICW2~4 映射到 0x21 地址
   */
  io_out8(0x20, 0x11);
  io_out8(0x21, 0x20);  /* 设置主芯片的中断向量范围，0x20~0x27 */
  io_out8(0x21, 0x04);  /* 主芯片的 IR2 与从芯片相连 */
  io_out8(0x21, 0x01);
  /**
   * 初始化 8259A-salva ICW1-4
   * 1. 从芯片的 ICW1 映射到 0xa0 地址
   * 2. 从芯片的 ICW2~4 映射到 0xa1 地址
   */
  io_out8(0xa0, 0x11);
  io_out8(0xa1, 0x28);  /* 设置从芯片的中断向量范围，0x28~0x2f */
  io_out8(0xa1, 0x02);  /* 从芯片的 IR1 与主芯片相连 */
  io_out8(0xa1, 0x01);

  /**
   * 复位 master/slave 的 IMR(OCW1) 寄存器的全部中断屏蔽位，并使能中断
   * 1. master OCW1 映射到 0x21
   * 2. slave OCW1 映射到 0xa1
   */
  /* 下面暂时屏蔽除了键盘中断之外所有中断 */
  io_out8(0x21, 0xfd);
  io_out8(0xa1, 0xff);

  sti();
}

/**
 * @brief 中断处理函数的主函数，作用是分发中断请求到各个中断处理函数
 * 所有的中断处理函数在执行完入口部分之后，都会跳转到这个主函数
 * 然后由主函数分发处理具体的中断，执行具体的中断处理函数
 * @param regs 
 * @param nr 
 */
void do_IRQ(unsigned long regs, unsigned long nr) {
  unsigned char x;
  color_printk(RED, BLACK, "do_IRQ:%#08x\t", nr);
  x = io_in8(0x60); /* 读取键盘缓冲区 */
  color_printk(RED, BLACK, "key code:%#08x\n", x);
  io_out8(0x20, 0x20);  /* 中断结束，发送 EIO 命令给 8259A 来复位 ISR 的对应位 */
}