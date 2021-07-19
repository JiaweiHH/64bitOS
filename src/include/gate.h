#ifndef __GATE_H_
#define __GATE_H_

/* 段描述符，每个 8B */
struct desc_struct {
  unsigned char x[8];
};

/* 中断描述符，每个 16B */
struct gate_struct {
  unsigned char x[16];
};

extern struct desc_struct GDT_Table[];
extern struct gate_struct IDT_Table[];
extern unsigned int TSS64_Table[26];

/*
// 127~64bit
Bit:     | 127              96| 95 							64|
Content: | 		  reserved      |   offset 63:32    |
// 63~32bit
Bit:     | 63              48 | 47 | 46 45 | 44 | 43 42 41 40 | 39 38 37 | 36 35 | 34 33 32 |
Content: | offset 31:16       | P  | DPL   | 0  |     TYPE    |  0  0  0 |  0  0 |    IST   |
// 31~0bit
Bit:     | 31              16 | 15              0 |
Content: | segment selector   | offset 15:00      |
*/

// /* rdx 初始保存的是 code_addr 的地址，rax = 0x80000 */
// "andq   $0x7,   %%rcx   \n\t"    /* rcx 保存了 ist，ist 占 3bit，所以先清楚 rcx 其余的 bit */
// "addq   %4,     %%rcx   \n\t"    /* rcx 15~8bit 保存 attr */
// "shlq   $32,    %%rcx   \n\t"    /* rcx 47~40bit 保存 attr，34~32bit 保存 ist */
// "addq   %%rcx,  %%rax   \n\t"    /* rax 47~40bit 保存 attr，34~32bit 保存 ist，15~0bit 保存 code_addr 的 15~0bit */
// "xorq   %%rcx,  %%rcx   \n\t"    /* 清空 rcx */
// "movl   %%edx,  %%ecx   \n\t"    /* ecx 现在保存 code_addr 的 31~0bit 了 */
// "shrq   $16,    %%rcx   \n\t"    /* rcx 15~0bit 保存 code_addr 31~16bit */
// "shlq   $48,    %%rcx   \n\t"    /* rcx 63~48bit 保存 code_addr 31~16bit */
// "addq   %%rcx,  %%rax   \n\t"    /* rax 63~48bit 保存 code_addr 31~16bit，47~40bit 保存 attr，34~32bit 保存 ist，15~0bit 保存 code_addr 的 15~0bit */
// "movq   %%rax,  %0      \n\t"    /* 到这里中断描述符的第一个字节设置完毕了，写入 IDT */
// "shrq   $32,    %%rdx   \n\t"    /* rdx 设置为 code_addr 的 63~32bit */
// "movq   %%rdx,  %1      \n\t"    /* 将 rdx 写入 IDT */
/**
 * @gate_selector_addr: IDT 中的某一个 entry
 * @attr: 0x8E 或 0x8F
 * @ist: 见 set_xxx_gate 函数说明
 * @code_addr: 处理函数地址
 * 
 * 这里的匹配约束例如 "3", "2" 表示该变量的约束和 %3, %2 变量的约束一样
 */
#define _set_gate(gate_selector_addr, attr, ist, code_addr)                    \
  do {                                                                         \
    unsigned long __d0, __d1;                                                  \
    __asm__ __volatile__("movw   %%dx,   %%ax    \n\t"                         \
                         "andq   $0x7,   %%rcx   \n\t"                         \
                         "addq   %4,     %%rcx   \n\t"                         \
                         "shlq   $32,    %%rcx   \n\t"                         \
                         "addq   %%rcx,  %%rax   \n\t"                         \
                         "xorq   %%rcx,  %%rcx   \n\t"                         \
                         "movl   %%edx,  %%ecx   \n\t"                         \
                         "shrq   $16,    %%rcx   \n\t"                         \
                         "shlq   $48,    %%rcx   \n\t"                         \
                         "addq   %%rcx,  %%rax   \n\t"                         \
                         "movq   %%rax,  %0      \n\t"                         \
                         "shrq   $32,    %%rdx   \n\t"                         \
                         "movq   %%rdx,  %1      \n\t"                         \
                         : "=m"(*((unsigned long *)(gate_selector_addr))),     \
                           "=m"(*(1 + (unsigned long *)(gate_selector_addr))), \
                           "=&a"(__d0), "=&d"(__d1)                            \
                         : "i"(attr << 8), "3"((unsigned long *)(code_addr)),  \
                           "2"(0x8 << 16), "c"(ist)                            \
                         : "memory");                                          \
  } while (0)

/* 将 TSS 段描述符的段选择子加载到 TR 寄存器 */
#define load_TR(n)                                                             \
  do {                                                                         \
    __asm__ __volatile__("ltr	%%ax" : : "a"(n << 3) : "memory");             \
  } while (0)

/* 配置 TSS 段内的 RSP 和 IST 项 */
void set_tss64(unsigned long rsp0, unsigned long rsp1, unsigned long rsp2,
               unsigned long ist1, unsigned long ist2, unsigned long ist3,
               unsigned long ist4, unsigned long ist5, unsigned long ist6,
               unsigned long ist7) {
  *(unsigned long *)(TSS64_Table + 1) = rsp0;
  *(unsigned long *)(TSS64_Table + 3) = rsp1;
  *(unsigned long *)(TSS64_Table + 5) = rsp2;

  *(unsigned long *)(TSS64_Table + 9) = ist1;
  *(unsigned long *)(TSS64_Table + 11) = ist2;
  *(unsigned long *)(TSS64_Table + 13) = ist3;
  *(unsigned long *)(TSS64_Table + 15) = ist4;
  *(unsigned long *)(TSS64_Table + 17) = ist5;
  *(unsigned long *)(TSS64_Table + 19) = ist6;
  *(unsigned long *)(TSS64_Table + 21) = ist7;
}

/**
 * 下面三组函数的功能都是设置门描述符，
 * @n: IDT 索引
 * @ist: IST = 0 时使用原有的栈切换机制，否则使用 IST 机制，IST 切换中断栈指针时不会考虑特权级切换
 * @addr: 处理函数地址
 */
/* 中断门描述符设置 */
static inline void set_intr_gate(unsigned int n, unsigned char ist, void *addr) {
  _set_gate(IDT_Table + n, 0x8E, ist, addr); /* P, DPL = 0，TYPE = E */
}

/* 陷阱门描述符设置 */
static inline void set_trap_gate(unsigned int n, unsigned char ist, void *addr) {
  _set_gate(IDT_Table + n, 0x8F, ist, addr); /* P, DPL = 0，TYPE = F */
}

/* 系统调用？ */
static inline void set_system_gate(unsigned int n, unsigned char ist, void *addr) {
  _set_gate(IDT_Table + n, 0xEF, ist, addr); /* P, DPL = 3，TYPE = F */
}

#endif