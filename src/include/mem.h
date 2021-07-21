#ifndef __MEMORY_H_
#define __MEMORY_H_

#include "printk.h"
#include "lib.h"

#define PTRS_PER_PAGE 512   /* 页表项个数 */
#define PAGE_OFFSET ((unsigned long)0xffff800000000000)   /* 内核层的起始线性地址 */

#define PAGE_GDT_SHIFT 39

/* 不同页大小的位数 */
#define PAGE_1G_SHIFT  30
#define PAGE_2M_SHIFT  21
#define PAGE_4K_SHIFT  12

/* 不同页大小的容量 */
#define PAGE_2M_SIZE (1UL << PAGE_2M_SHIFT)
#define PAGE_4K_SIZE (1UL << PAGE_4K_SHIFT)

/* 可以用于下取整 */
#define PAGE_2M_MASK (~(PAGE_2M_SIZE - 1))
#define PAGE_4K_MASK (~(PAGE_4K_SIZE - 1))

/* 用于将 addr 按照 page_size 上取整对齐 */
#define PAGE_2M_ALIGN(addr) ((unsigned long)(addr) + PAGE_2M_SIZE - 1) & PAGE_2M_MASK
#define PAGE_4K_ALIGN(addr) ((unsigned long)(addr) + PAGE_4K_SIZE - 1) & PAGE_4K_MASK

/* 虚拟地址和物理地址相互转换 */
#define virt_to_phy(addr) ((unsigned long)(addr) - PAGE_OFFSET)
#define phy_to_virt(addr) ((unsigned long *)((unsigned long)(addr) + PAGE_OFFSET))

int ZONE_DMA_INDEX = 0;
int ZONE_NORMAL_INDEX = 0;  // low 1GB RAM，已经在页表里面映射
int ZONE_UNMAPED_INDEX = 0; // above 1GB RAM，没有经过页表映射

#define MAX_NR_ZONES 10 // max zone
#define ZONE_DMA (1 << 0)
#define ZONE_NORMAL (1 << 1)
#define ZONE_UNMAPED (1 << 2)

/* 页面属性 */
#define PG_PTable_Maped (1 << 0)
#define PG_Kernel_Init (1 << 1)
#define PG_Referenced (1 << 2)
#define PG_Dirty (1 << 3)
#define PG_Active (1 << 4)
#define PG_Up_To_Date (1 << 5)
#define PG_Device (1 << 6)
#define PG_Kernel (1 << 7)
#define PG_K_Share_To_U (1 << 8)
#define PG_Slab (1 << 9)

/**
 * @brief 内存解析结构体，大小为 20B
 * 0x00 开始的 8B 是起始地址 address
 * 0x08 开始的 8B 是长度 length
 * 0x10 开始的 4B 是类型 type
 * 
 * packed 修饰该结构体不会生成对齐空间
 */
struct E820 {
  unsigned long address;
  unsigned long length;
  /**
   * 0x01: 可用物理内存
   * 0x02: 保留或无效值（包括 ROM、设备内存）
   * 0x03: ACPI 的回收内存
   * 其他值: 未定义，保留使用
   */
  unsigned int type;
}__attribute__((packed));

/**
 * @brief 保存所有关于内存的信息，用于内存管理
 */
struct Global_Memory_Descriptor {
  struct E820 e820[32];         /* 物理内存段结构数组 */
  unsigned long e820_length;    /* 段结构数组长度 */

  unsigned long *bits_map;      /* 物理地址空间页映射位图 */
  unsigned long bits_size;      /* 页数量 */
  unsigned long bits_length;    /* 位图长度，以字节为单位 */

  struct page *pages_struct;    /* 指向全局 page 结构体数组 */
  unsigned long pages_size;     /* page 结构体总数，等于页面数量 */
  unsigned long pages_length;   /* page 结构体数组长度，以字节为单位 */

  struct zone *zones_struct;    /* 指向全局 zone 结构体数组 */
  unsigned long zones_size;     /* zone 结构体数量 */
  unsigned long zones_length;   /* zone 结构体数组长度 */

  /**
   * start_code: 内核程序其实代码段地址
   * end_code: 内核程序结束代码段地址
   * end_data: 内核程序结束数据段地址
   * end_brk: 内核程序的结束地址
   * end_of_struct: 内存页管理结构的结尾地址
   */
  unsigned long start_code, end_code, end_data, end_brk;
  unsigned long end_of_struct;
};

/**
 * @brief 物理内存区域管理结构体
 */
struct zone {
  struct page *pages_group;           /* page 数组 */
  unsigned long pages_length;         /* zone 包含的 page 数量 */
  unsigned long zone_start_address;   /* zone 的起始页对齐地址 */
  unsigned long zone_end_address;     /* zone 的结束页对齐地址 */
  unsigned long zone_length;          /* zone 经过页对齐之后的地址长度 */
  unsigned long attribute;            /* zone 的属性 */

  struct Global_Memory_Descriptor *GMD_struct;  /* 指向全局管理结构体 */

  unsigned long page_using_count;     /* 已使用的物理内存页数量 */
  unsigned long page_free_count;      /* 空闲物理内存页数量 */
  unsigned long total_pages_link;     /* 本区域物理页被引用次数 */
};

/**
 * @brief 物理内存管理结构体
 */
struct page {
  struct zone *zone_struct;       /* 指向本页所属的 zone 结构体 */
  unsigned long PHY_address;      /* 页的物理地址 */
  unsigned long attribute;        /* 页的属性，映射状态、活动状态、使用者等信息 */
  unsigned long reference_count;  /* 该页的引用次数 */
  unsigned long age;              /* 该页的创建时间 */
};

void init_memory();
unsigned long page_init(struct page *page, unsigned long flags);
unsigned long page_clean(struct page *page);
struct page *alloc_pages(int zone_select, int number, unsigned long page_flags);
extern struct Global_Memory_Descriptor memory_management_struct;
extern unsigned long *Global_CR3;

/* 获取页目录地址 */
static inline unsigned long *get_gdt() {
  unsigned long *tmp;
  __asm__ __volatile__("movq %%cr3, %0\n\t" : "=r"(tmp) : : "memory");
  return tmp;
}

/* 刷新 TLB，只需要重新加载 CR3 寄存器即可 */
static inline void flush_tlb() {
  unsigned long tmpreg;
  __asm__ __volatile__("movq %%cr3, %0\n\t"
                       "movq %0, %%cr3\n\t"
                       : "=r"(tmpreg)
                       :
                       : "memory");
}

#endif