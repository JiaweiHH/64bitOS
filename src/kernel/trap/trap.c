#include "trap.h"
#include "gate.h"

/* 0 #DE. 除法错误 */
void do_divide_error(unsigned long rsp, unsigned long error_code) {
  unsigned long *p = NULL;
  p = (unsigned long *)(rsp + 0x98); /* 0x98 是 RIP 相对于 RSP 的栈上偏移 */
  /* 显示错误码值、栈指针值、异常产生的程序地址等日志信息 */
  color_printk(RED, BLACK, "do_divide_error(0), ERROR_CODE: %#018lx, RSP: %#018lx, RIP: %#018lx\n", error_code, rsp, *p);
  while(1);
}

/* 2 NMI 不可屏蔽中断 */
void do_nmi(unsigned long rsp, unsigned long error_code) {
  unsigned long *p = NULL;
  p = (unsigned long *)(rsp + 0x98); /* 0x98 是 RIP 相对于 RSP 的栈上偏移 */
  color_printk(RED, BLACK, "do_nmi(2), ERROR_CODE: %#018lx, RSP: %#018lx, RIP: %#018lx\n", error_code, rsp, *p);
  while(1);
}

/* 10 #TS. 无效的 TSS 段 */
void do_invalid_TSS(unsigned long rsp, unsigned long error_code) {
  unsigned long *p = NULL;
  p = (unsigned long *)(rsp + 0x98);
  color_printk(RED, BLACK, "do_invalid_TSS(10), ERROR_CODE: %#018lx, RSP: %#018lx, RIP: %#018lx\n", error_code, rsp, *p);

  if (error_code & 0x1) {   /* 错误码的第 0 位被置位，说明异常是在向程序投递外部事件的过程中触发 */
    color_printk(
        RED, BLACK,
        "The execption occurred during delivery of an event external to the "
        "program, such as an interrupt or an ealier execption.\n");
  }
  if (error_code & 0x2)     /* 错误码的第 1 位被置位，说明错误码的段选择子部分记录的是 IDT 内的门描述符 */
    color_printk(RED, BLACK, "Refers to a gate descriptor in the IDT;\n");
  else                      /* 第 1 位复位，错误码段选择子部分记录的是 LDT 或 GDT 的描述符 */
    color_printk(RED, BLACK,
                 "Refers to a descriptor in the GDT or the current LDT;\n");
  if ((error_code & 0x2) == 0) {
    if (error_code & 0x4)   /* 第 1 位复位、第 2 位置位，段选择子记录的是 LDT 内的描述符 */
      color_printk(RED, BLACK, "Refers to a segment or gate descriptor in the LDT;\n");
    else                    /* 第 1 位复位、第 2 位复位，段选择子记录的是 GDT 内的描述符 */
      color_printk(RED, BLACK, "Refers to a descriptor in the current GDT;\n");
  }
  /* 错误码的 15~3bit 记录的是段选择子 */
  color_printk(RED, BLACK, "Segment Selector Index: %#010x\n", error_code & 0xfff8);
  while(1);
}

/* 14 #PF. 页错误异常 */
void do_page_fault(unsigned long rsp, unsigned long error_code) {
  unsigned long *p = NULL;
  unsigned long cr2 = 0;
  /* 获取 CR2 寄存器的值，CR2 寄存器保存了触发异常时的线性地址 */
  __asm__ __volatile__("movq %%cr2, %0" : "=r"(cr2)::"memory");
  p = (unsigned long *)(rsp + 0x98);
  color_printk(RED, BLACK, "do_page_fault(14), ERROR_CODE: %#018lx, RSP: %#018lx, RIP: %#018lx\n", error_code, rsp, *p);

  /* P = 0，表示页不存在引发的异常 */
  if (!(error_code & 0x01))
    color_printk(RED, BLACK, "Page Not-Present,\t");

  /* W/R = 0 读取页引发的异常，W/R = 1 写入页引发的异常 */
  if (error_code & 0x02)
    color_printk(RED, BLACK, "Write Cause Fault,\t");
  else
    color_printk(RED, BLACK, "Read Cause Fault,\t");

  /* U/S = 0 超级用户权限访问页引发异常，U/S = 1 普通权限访问页引发异常 */
  if (error_code & 0x04)
    color_printk(RED, BLACK, "Fault in user(3)\t");
  else
    color_printk(RED, BLACK, "Fault in supervisor(0, 1, 2)\t");

  /* RSVD = 1 置位页表项保留位引发异常 */
  if (error_code & 0x08)
    color_printk(RED, BLACK, ", Reserved Bit Cause Fault\t");

  /* I/D = 1 获取指令时引发异常 */
  if (error_code & 0x10)
    color_printk(RED, BLACK, ", Instruction fetch Cause Fault");

  color_printk(RED, BLACK, "\n");
  color_printk(RED, BLACK, "CR2: %#018lx\n", cr2);
  while(1);
}

// void divide_error() {}
// void invalid_TSS() {}
// void page_fault() {}

/* 设置 IDT 的各个表项 */
void sys_vector_init() {
  set_trap_gate(0, 1, divide_error);
  // set_trap_gate(1, 1, debug);
  // set_intr_gate(2, 1, nmi);
  // set_system_gate(3, 1, int3);
  // set_system_gate(4, 1, overflow);
  // set_system_gate(5, 1, bounds);
  // set_trap_gate(6, 1, undefined_opcode);
  // set_trap_gate(7, 1, dev_not_available);
  // set_trap_gate(8, 1, double_fault);
  // set_trap_gate(9, 1, coprocessor_segment_overrun);
  set_trap_gate(10, 1, invalid_TSS);
  // set_trap_gate(11, 1, segment_not_present);
  // set_trap_gate(12, 1, stack_segment_fault);
  // set_trap_gate(13, 1, general_protection);
  set_trap_gate(14, 1, page_fault);

  // 15 Intel reserved. Do not use.

  // set_trap_gate(16, 1, x87_FPU_error);
  // set_trap_gate(17, 1, alignment_check);
  // set_trap_gate(18, 1, machine_check);
  // set_trap_gate(19, 1, SIMD_exception);
  // set_trap_gate(20, 1, virtualization_exception);

  // set_system_gate(SYSTEM_CALL_VECTOR,7,system_call);
}