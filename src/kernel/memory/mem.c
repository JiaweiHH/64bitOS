#include "mem.h"
#include "lib.h"

unsigned long *Global_CR3 = NULL;

static void bitmap_init() {
  /* 计算物理内存结束地址，这还包含了内存空洞和 ROM 地址空间 */
  unsigned long TotalMem = memory_management_struct.e820[memory_management_struct.e820_length].address +
             memory_management_struct.e820[memory_management_struct.e820_length].length;
  /* bitmap 保存地址相对于内核结束地址留一小段空间，这段空间按照 4K 对齐 */
  memory_management_struct.bits_map =
      (unsigned long *)((memory_management_struct.end_brk + PAGE_4K_SIZE - 1) &
                        PAGE_4K_MASK);
  /* 页数量 */
  memory_management_struct.bits_size = TotalMem >> PAGE_2M_SHIFT;
  /**
   * 计算 bitmap 长度，大小为字节
   * 1. bits_size = TotalMem >> PAGE_2M_SHIFT 表示需要多少 bit
   * 2. (bits_size + 63) / 8 & (~(sizeof(long) - 1)) 表示按照 long 的大小上取整结果的字节大小，例如需要 1bit 结果就是 64bit = 8B
   * 3. 其中最后的 &(~7) 是为了去掉上一步多计算的结果，例如如果需要 64bit，上一步计算出来的就是 15B，经过这一步计算之后会变成 8B 也就是我们需要的结果
   */
  memory_management_struct.bits_length =
      (((unsigned long)(TotalMem >> PAGE_2M_SHIFT) + sizeof(long) * 8 - 1) / 8) &
      (~(sizeof(long) - 1));
  /* 将所有的 bit 置为 1，目的是为了标注非内存页（内存空洞和 ROM 空间）已被使用，之后再把可用物理页的 bit 复位 */
  memset(memory_management_struct.bits_map, 0xff, memory_management_struct.bits_length);
}

static void pages_init() {
  /* 计算物理内存结束地址，这还包含了内存空洞和 ROM 地址空间 */
  unsigned long TotalMem = memory_management_struct.e820[memory_management_struct.e820_length].address +
             memory_management_struct.e820[memory_management_struct.e820_length].length;
  /* pages[] 起始地址相对于 bitmap 结束地址留一小段空间，按照 4K 对齐 */
  memory_management_struct.pages_struct =
      (struct page *)(((unsigned long)memory_management_struct.bits_map +
                       memory_management_struct.bits_length + PAGE_4K_SIZE - 1) & PAGE_4K_MASK);
  memory_management_struct.pages_size = TotalMem >> PAGE_2M_SHIFT;
  /**
   * 计算 pages[] 数组的大小，大小为字节
   * 1. tmp = (TotalMem >> PAGE_2M_SHIFT) * sizeof(struct page) 计算需要多少字节
   * 2. (tmp + sizeof(long) - 1) & (~(sizeof(long) - 1) 表示按照 long 的大小上取整对齐
   */
  memory_management_struct.pages_length = ((TotalMem >> PAGE_2M_SHIFT) * sizeof(struct page) + sizeof(long) - 1) & (~(sizeof(long) - 1));
  /* 清零 pages[] 数组，以备后续的初始化程序使用 */
  memset(memory_management_struct.pages_struct, 0x00, memory_management_struct.pages_length);
}

static void zones_init() {
  /* zones[] 起始地址相对于 pages[] 结束地址留一小段空间，按照 4K 对齐 */
  memory_management_struct.zones_struct =
      (struct zone *)(((unsigned long)memory_management_struct.pages_struct +
                       memory_management_struct.pages_length + PAGE_4K_SIZE - 1) & PAGE_4K_MASK);
  /* 由于目前暂时无法计算出 zones[] 的元素个数，暂时先将 zones_size 设置为 0 */
  memory_management_struct.zones_size = 0;
  /* 暂时先将 zones_length 设置为 5 个 struct zone 的大小 */
  memory_management_struct.zones_length = (5 * sizeof(struct zone) + sizeof(long) - 1) & (~(sizeof(long) - 1));
  memset(memory_management_struct.zones_struct, 0x00, memory_management_struct.zones_length);
}

/**
 * @brief 初始化可用物理内存
 * 所有的 struct page 结构体连接在一起，首地址是 memory_management_struct.pages_struct
 * 所有的 struct zone 结构体连接在一起，首地址是 memory_management_struct.zones_struct
 * 每个 zone 对应 pages_struct 区间的某一小段区间，每个 page 都有其对应的 zone
 * 另外补充，这里第一页是从 2MB 开始的，0~2MB 的物理内存并没有被包含进去，详细可以运行内核观察一下物理内存段的输出结果
 */
static void init_usage_memory() {
  for(int i = 0; i <= memory_management_struct.e820_length; ++i) {
    unsigned long start, end;
    struct zone *z;
    struct page *p;
    unsigned long *b;

    /* 过滤非物理内存段 */
    if(memory_management_struct.e820[i].type != 1)
      continue;
    /* 页对齐 */
    start = PAGE_2M_ALIGN(memory_management_struct.e820[i].address);
    end = ((memory_management_struct.e820[i].address + memory_management_struct.e820[i].length) & PAGE_2M_MASK);
    if(end <= start)  /* 第一个可用物理内存段会被忽略掉，因为对齐之后 end < start */
      continue;
    
    /* zone init，一个 zone 对应一个可用物理内存段 */
    z = memory_management_struct.zones_struct + memory_management_struct.zones_size;
    memory_management_struct.zones_size++;

    z->zone_start_address = start;
    z->zone_end_address = end;
    z->zone_length = end - start;
    z->page_using_count = 0;
    z->page_free_count = (end - start) >> PAGE_2M_SHIFT;
    z->total_pages_link = 0;
    z->attribute = 0;
    z->GMD_struct = &memory_management_struct;
    z->pages_length = (end - start) >> PAGE_2M_SHIFT;
    /**
     * zone 对应的 page 数组首地址
     * 如果是第 0 个 zone 的话，pages_group 指向的是第 1 页而不是第 0 页，原因在函数开头已经说明
     */
    z->pages_group = (struct page *)(memory_management_struct.pages_struct + (start >> PAGE_2M_SHIFT));
    
    /* page init */
    p = z->pages_group;
    for(int j = 0; j < z->pages_length; ++j, ++p) {   /* 遍历该段内的所有 page，初始化 page 结构体，复位 bitmap bit 位 */
      p->zone_struct = z;   /* 第 j 个 page 对应的 zone[] */
      p->PHY_address = start + PAGE_2M_SIZE * j;  /* 第 j 个 page 的首地址 */
      p->attribute = 0;
      p->reference_count = 0;
      p->age = 0;
      /**
       * 复位 bitmap 对应的 bit 位
       * 1. (p->PHY_address >> PAGE_2M_SHIFT) >> 6 计算出该 page 在 bitmap 中的第几个 long
       * 2. ^= 1UL << (p->PHY_address >> PAGE_2M_SHIFT) % 64 对该位进行异或操作完成复位
       */
      *(memory_management_struct.bits_map + ((p->PHY_address >> PAGE_2M_SHIFT) >> 6)) ^=
          1UL << (p->PHY_address >> PAGE_2M_SHIFT) % 64;
    }
  }
}

unsigned long page_init(struct page *page, unsigned long flags) {
  if(!page->attribute) {
    /**
     * 页面属性为空，则占用该页面
     */
    /* 置位 bitmap 对应的 bit 位 */
    *(memory_management_struct.bits_map + ((page->PHY_address >> PAGE_2M_SHIFT) >> 6)) |=
        1UL << (page->PHY_address >> PAGE_2M_SHIFT) % 64;
    page->attribute = flags;                /* 设置页面属性 */
    page->reference_count++;                /* 该页面引用计数增加 */
    page->zone_struct->page_using_count++;  /* 增加 zone 已使用页面数量 */
    page->zone_struct->page_free_count--;   /* 减少空闲页面数量 */
    page->zone_struct->total_pages_link++;  /* 本区域页面引用计数增加 */
  } else if ((page->attribute & PG_Referenced) ||
             (page->attribute & PG_K_Share_To_U) || (flags & PG_Referenced) ||
             (flags & PG_K_Share_To_U)) {
    /**
     * 如果当前页面结构属性或者参数 flags 中含有引用属性或者共享属性，
     * 那么就只增加 page 结构体的引用技术和 zone 结构体的引用计数
     */
    page->attribute |= flags;
    page->reference_count++;                /* 增加页面引用计数 */
    page->zone_struct->total_pages_link++;  /* 增加本区域内的页面引用计数 */
  } else {  // TODO
    *(memory_management_struct.bits_map + ((page->PHY_address >> PAGE_2M_SHIFT) >> 6)) |=
        1UL << (page->PHY_address >> PAGE_2M_SHIFT) % 64;
  }
}

static void mem_log_print() {
  color_printk(
      ORANGE, BLACK, "bits_map: %#018lx, bits_size: %#018lx, bits_length: %#018lx\n",
      memory_management_struct.bits_map, memory_management_struct.bits_size,
      memory_management_struct.bits_length);
  color_printk(ORANGE, BLACK,
               "pages_struct: %#018lx, pages_size: %#018lx, pages_length: %#018lx\n",
               memory_management_struct.pages_struct,
               memory_management_struct.pages_size,
               memory_management_struct.pages_length);
  color_printk(ORANGE, BLACK,
               "zones_struct: %#018lx, zones_size: %#018lx, zones_length: %#018lx\n",
               memory_management_struct.zones_struct,
               memory_management_struct.zones_size,
               memory_management_struct.zones_length);
  ZONE_DMA_INDEX = ZONE_NORMAL_INDEX = 0;
  for(int i = 0; i < memory_management_struct.zones_size; ++i) {
    struct zone *z = memory_management_struct.zones_struct + i;
    color_printk(ORANGE, BLACK,
                 "zone_start_address: %#018lx, zone_end_address: %#018lx, zone_"
                 "length: %#018lx, pages_group: %#018lx, pages_length: %#018lx\n",
                 z->zone_start_address, z->zone_end_address, z->zone_length,
                 z->pages_group, z->pages_length);
    if(z->zone_start_address == 0x100000000)  /* 1GB 以上的物理内存没有经过页表映射 */
      ZONE_UNMAPED_INDEX = i;                 /* 记录该 zone */
  }
}

static void management_page_init() {
  color_printk(ORANGE, BLACK,
      "start_code: %#018lx, end_code: %#018lx, end_data: %#018lx, end_brk: %#018lx, "
      "end_of_struct: %#018lx\n",
      memory_management_struct.start_code, memory_management_struct.end_code,
      memory_management_struct.end_data, memory_management_struct.end_brk,
      memory_management_struct.end_of_struct);
  unsigned int end_page_num = virt_to_phy(memory_management_struct.end_of_struct) >> PAGE_2M_SHIFT;
  /**
   * 将系统内核与内存管理单元所占用的物理页对应的 page 结构体全部初始化，page_init 函数会完成
   * 1. 占用 bitmap
   * 2. 设置 page 和 zone 结构属性
   */
  for(int j = 0; j < end_page_num; ++j) {
    page_init(memory_management_struct.pages_struct + j,
              PG_PTable_Maped | PG_Kernel_Init | PG_Active | PG_Kernel);
  }
}

void init_memory() {
  int i, j;
  unsigned long TotalMem = 0;
  struct E820 *p = 0;
  color_printk(BLUE, BLACK,
               "Display Physics Address MAP, Type(1:RAM, 2:ROM or Reserved, "
               "3:ACPI Reclaim "
               "Memory, 4:ACPI NVS Memory, Others:Undefine)\n");
  p = (struct E820 *)0xffff800000007e00; /* BIOS 中断获取的内存信息保存地址 */

  for (int i = 0; i < 32; ++i) {
    color_printk(ORANGE, BLACK,
                 "Address: %#018lx\tLength: %#018lx\tType: %#010x\n", p->address,
                 p->length, p->type);
    /* 如果该内存是可用内存类型，计入总内存 */
    if (p->type == 1) {
      TotalMem += p->length;
    }

    /* 保存内存空间分布信息到全局变量中 */
    memory_management_struct.e820[i].address += p->address;
    memory_management_struct.e820[i].length += p->length;
    memory_management_struct.e820[i].type = p->type;
    memory_management_struct.e820_length = i;

    ++p;
    
    /* 第一次遇到无效的内存段就停止 */
    if (p->type > 4 || p->length == 0 || p->type < 1)
      break;
  }
  color_printk(ORANGE, BLACK, "OS Can Used Total RAM: %#018lx\n", TotalMem);
  
  /* 统计可用的物理页数量 */
  TotalMem = 0;
  for(int i = 0; i < 32; ++i) {
    unsigned long start, end;
    if(memory_management_struct.e820[i].type != 1)
      continue;
    /* 将结束地址和起始地址按照 page_size 对齐 */
    start = PAGE_2M_ALIGN(memory_management_struct.e820[i].address);  // start 按照上边界对齐
    end = ((memory_management_struct.e820[i].address + memory_management_struct.e820[i].length) >> PAGE_2M_SHIFT) << PAGE_2M_SHIFT;   // end 按照下边界对齐
    if(end <= start)
      continue;
    TotalMem += (end - start) >> PAGE_2M_SHIFT;
  }
  color_printk(ORANGE, BLACK, "OS Can Used Total 2M PAGEs: %#010x = %010d\n", TotalMem, TotalMem);

  /**
   * 初始化 bit_map, pages, zones 信息
   * 所有信息从内核程序结束地址开始，即 end_brk 之后开始
   */

  /* 1. 首先初始化 bitmap 信息 */
  bitmap_init();
  /* 2. 初始化 page[] 结构体数组 */  
  pages_init();
  /* 3. 初始化 zones[] 结构体数组 */
  zones_init();
  /* 4. 遍历全部内存段信息，初始化可用物理内存段的 bitmap, zones[], pages[] */
  init_usage_memory();

  /* 由于 0~2MB 的物理内存并没有被初始化进去，这里需要对该页进行特殊初始化 */
  memory_management_struct.pages_struct->zone_struct = memory_management_struct.zones_struct; 
  memory_management_struct.pages_struct->PHY_address = 0UL;
  memory_management_struct.pages_struct->attribute = 0;
  memory_management_struct.pages_struct->reference_count = 0;
  memory_management_struct.pages_struct->age = 0;
  /* 计算 zones[] 占用的字节大小 */
  memory_management_struct.zones_length = (memory_management_struct.zones_size * sizeof(struct zone) + sizeof(long) - 1) & (~(sizeof(long) - 1));

  /* 打印各结构的统计信息 */
  mem_log_print();

  /* 设置内存管理结构的结束地址，预留一段空间防止越界访问 */
  memory_management_struct.end_of_struct =
      (unsigned long)((unsigned long)memory_management_struct.zones_struct +
      memory_management_struct.zones_length + sizeof(long) * 32) &
      (~(sizeof(long) - 1));

  /* 初始化内存管理结构所占的物理页的 page 结构体 */
  management_page_init();

  /* 清空用于一致性映射的页表项，用于线性地址 0 开始的地址映射的页表项 */
  Global_CR3 = get_gdt();
  color_printk(INDIGO, BLACK, "Global_CR3\t: %#018lx\n", Global_CR3);   /* 打印 PML4E 首地址 */
  color_printk(INDIGO, BLACK, "*Global_CR3\t: %#018lx\n",               /* 打印 PDPTE 首地址 */
               *phy_to_virt(Global_CR3) & (~0xff));
  color_printk(PURPLE, BLACK, "**Global_CR3\t: %#018lx\n",              /* 打印 PDE 首地址 */
               *phy_to_virt(*phy_to_virt(Global_CR3) & (~0xff)) & (~0xff));
  for(int i = 0; i < 10; ++i) {   /* 清空 PML4E 的前 10 个 entry（其实只要第一个就好了） */
    *(phy_to_virt(Global_CR3) + i) = 0UL;
  }
  flush_tlb();
}

/**
 * @brief 分页物理页，一次最多分配 64 页
 * 
 * @param zone_select 从 DMA, NORMAL 等区域选择 ZONE
 * @param number number <= 64
 * @param page_flags struct page 属性
 * @return struct page* 
 */
struct page *alloc_pages(int zone_select, int number, unsigned long page_flags) {
  unsigned long page = 0;
  int zone_start = 0, zone_end = 0;
  /* 选择 ZONE 区域 */
  switch (zone_select) {
  case ZONE_DMA:
    zone_start = 0;
    zone_end = ZONE_DMA_INDEX;
    break;
  case ZONE_NORMAL:
    zone_start = ZONE_DMA_INDEX;
    zone_end = ZONE_NORMAL_INDEX;
    break;
  case ZONE_UNMAPED:
    zone_start = ZONE_UNMAPED_INDEX;
    zone_end = memory_management_struct.zones_size - 1;
    break;
  default:
    color_printk(RED, BLACK, "alloc_pages error zone_select index\n");
    return NULL;
    break;
  }
  for(int i = zone_start; i <= zone_end; ++i) {
    struct zone *z;
    unsigned long start, end, length;
    unsigned long tmp;
    /* 区域中没有足够的页面 */
    if((memory_management_struct.zones_struct + i)->page_free_count < number)
      continue;
    z = memory_management_struct.zones_struct + i;
    start = z->zone_start_address >> PAGE_2M_SHIFT;
    end = z->zone_end_address >> PAGE_2M_SHIFT;
    length = z->zone_length >> PAGE_2M_SHIFT;

    /**
     * 为了按照 unsigned long 对齐，起始页开始的位图只能检索 tmp = 64 - start % 64 次
     * 通过 j += j % 64 ? tmp : 64，可以将后续的步进过程按照 unsigned long 对齐
     */
    tmp = 64 - start % 64;
    for(unsigned long j = start; j <= end; j += j % 64 ? tmp : 64) {
      /* 确定一开始在 bitmap 的第几个 unsigned long */
      unsigned long *p = memory_management_struct.bits_map + (j >> 6);
      /* 确定此次 unsigned long 步长的起始偏移 */
      unsigned long shift = j % 64;

      /**
       * k < 64 - shift 确定这次搜索的区间长度，除了第一次其余都是 64
       * tmp = (*p >> k) | (*(p + 1) << (64 - k)) 表示将下一个 64bit 与当前 64bit 去掉前面 0~k-1 bit 之后的结果进行拼接
       * tmp & (number == 64 ? 0xffffffffffffffffUL : ((1UL << number) - 1)) 表示需要 number 个连续的 page 就保留 tmp 拼接结果的 number bit
       * 最后 ! 判断上述结果位图中是不是全是 0，只有全是 0 才是可以分配的连续页
       * 
       * 补充：
       * 1. 关于 &() 括号中的部分为什么不直接使用 (1UL << number) - 1，是由于对于 64bit 寄存器来说 SHL 指令的左移范围是 0~63
       * 2. 关于为什么需要拼接，是因为可能某一个 unsigned long 已经被占用了几个 bit 了，这样的话就需要下一个 unsigned long 来扩充
       */
      for(unsigned long k = shift; k < 64 - shift; ++k) {
        if(!(((*p >> k) | (*(p + 1) << (64 - k))) & (number == 64 ? 0xffffffffffffffffUL : ((1UL << number) - 1)))) {
          page = j + k - 1;   /* 获取连续页的起始页号 */
          for(unsigned long l = 0; l < number; ++l) {   /* 占用这些物理页面 */
            struct page *x = memory_management_struct.pages_struct + page + l;
            page_init(x, page_flags);
          }
          goto find_free_pages;
        }
      }
    }
  }
  return NULL;

find_free_pages:
  /* 返回连续页面的第一页 struct page 结构 */
  return (struct page *)(memory_management_struct.pages_struct + page);
}