#include "lib.h"
#include "printk.h"
#include "gate.h"

void Start_Kernel(void) {
  int *addr = (int *)0xffff800000a00000;
  int i;

  Pos.XResolution = 1440;
  Pos.YResolution = 900;
  Pos.XPosition = Pos.YPosition = 0;
  Pos.XCharSize = 8;
  Pos.YCharSize = 16;
  Pos.FB_addr = addr;
  Pos.FB_length = Pos.XResolution * Pos.YResolution * 4;

  for(i = 0; i < 1440 * 20; ++i) {
    *((char *)addr + 0) = (char)0x00;
    *((char *)addr + 1) = (char)0x00;
    *((char *)addr + 2) = (char)0xff;
    *((char *)addr + 3) = (char)0x00;
    ++addr;
  }
  for(i = 0; i < 1440 * 20; ++i) {
    *((char *)addr + 0) = (char)0x00;
    *((char *)addr + 1) = (char)0xff;
    *((char *)addr + 2) = (char)0x00;
    *((char *)addr + 3) = (char)0x00;
    ++addr;
  }
  for(i = 0; i < 1440 * 20; ++i) {
    *((char *)addr + 0) = (char)0xff;
    *((char *)addr + 1) = (char)0x00;
    *((char *)addr + 2) = (char)0x00;
    *((char *)addr + 3) = (char)0x00;
    ++addr;
  }
  for(i = 0; i < 1440 * 20; ++i) {
    *((char *)addr + 0) = (char)0xff;
    *((char *)addr + 1) = (char)0xff;
    *((char *)addr + 2) = (char)0xff;
    *((char *)addr + 3) = (char)0x00;
    ++addr;
  }

  color_printk(YELLOW, BLACK, "Hello World!\n");

  // load_TR(8);
  set_tss64(0xffff800000007c00, 0xffff800000007c00, 0xffff800000007c00,
            0xffff800000007c00, 0xffff800000007c00, 0xffff800000007c00,
            0xffff800000007c00, 0xffff800000007c00, 0xffff800000007c00,
            0xffff800000007c00);
  sys_vector_init();
  while(1)
    ;
}