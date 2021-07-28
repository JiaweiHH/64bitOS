#include "lib.h"
#include "printk.h"
#include "gate.h"
#include "trap.h"
#include "mem.h"
#include "interrupt.h"
#include "task.h"

/**
 * @brief 内核程序代码段和数据段的相关信息
 * 经过声明后的这些标识符会被链接脚本指定的地址，例如
 * _text 会被放在 0xffff 8000 0010 0000 地址处
 */
extern char _text;
extern char _etext;
extern char _edata;
extern char _end;

struct Global_Memory_Descriptor memory_management_struct = {{0}, 0};

void Start_Kernel(void) {
  int *addr = (int *)0xffff800000a00000;

  Pos.XResolution = 1440;
  Pos.YResolution = 900;
  Pos.XPosition = Pos.YPosition = 0;
  Pos.XCharSize = 8;
  Pos.YCharSize = 16;
  Pos.FB_addr = addr;
  Pos.FB_length = (Pos.XResolution * Pos.YResolution * 4 + PAGE_4K_SIZE - 1) & PAGE_4K_MASK;

  // load_TR(10);
  set_tss64(0xffff800000007c00, 0xffff800000007c00, 0xffff800000007c00,
            0xffff800000007c00, 0xffff800000007c00, 0xffff800000007c00,
            0xffff800000007c00, 0xffff800000007c00, 0xffff800000007c00,
            0xffff800000007c00);
  sys_vector_init();

  /* 初始化内核程序地址相关信息 */
  memory_management_struct.start_code = (unsigned long)&_text;
  memory_management_struct.end_code = (unsigned long)&_etext;
  memory_management_struct.end_data = (unsigned long)&_edata;
  memory_management_struct.end_brk = (unsigned long)&_end;
  
  color_printk(RED, BLACK, "memory_init\n");
  init_memory();

  color_printk(RED, BLACK, "interrupt init\n");
  init_interrupt();

  color_printk(RED, BLACK, "task_init\n");
  task_init();

  while(1)
    ;
}