[TOC]

# bootloader 引导程序

bootloader 引导程序分为 boot 程序和 loader 程序，boot 程序负责开机启动和加载 loader 程序；loader 程序则用于完成配置硬件工作环境、引导加载内核等任务

> 关于计算机上电 BIOS 的引导原理这里暂不讨论，参考 bzImage 和 boot 笔记

## boot

`boot` 程序位于软盘的第一个扇区，由 BIOS 加载到物理地址 `0x7c00` 处，`boot` 的一个重要的工作就是加载 `loader` 程序；为了加载 loader 程序，要么将 loader 放在固定的扇区，boot 每次都去加载该扇区的数据，但是这样会导致每次修改了 loader 并向存储介质写入都需要重新计算起始扇区和占用扇区数量。因此干脆直接将软盘格式化为 `FAT12` 文件系统，避免了每次都重新计算扇区的问题

> 格式化软盘为 FAT12，主要就是写入 FAT12 的引导扇区，类似于 ext2 文件系统的超级块；当我们在磁盘上初始化并写入了超级块的数据也就相当于将磁盘格式化成 ext2 文件系统了

FAT12 将存储介质划分成 引导扇区、FAT表、根目录区、数据区 四个部分

<img src="./pic/FAT12.png" alt="image-20210706204737804" style="zoom:50%;" />

- **引导扇区**，描述了 FAT12 文件系统的相关结构信息以及我们的 `boot` 程序
- **FAT 表**，文件按照簇分割，每簇对应 FAT 表中的一个表项，通过 FAT 表可以将分割的文件连接起来，FAT12 的 FAT 表项为 12bit
- **根目录区**，存储目录信息
- **数据区**，文件的数据

首先需要为软盘创建 FAT12 文件系统的引导扇区数据，软盘的第一个扇区既包含了 FAT12 的引导扇区数据，也包含了 `boot` 程序的数据和指令；引导扇区的前 62 bytes 是作为 `super_block` 使用的，并且在一开始是一条跳转代码，这是因为其后面的数据不是可执行程序而是 FAT12 的组成信息，因此必须要跳过这部分内容

跳转代码会跳转到真正的 `boot` 程序

```assembly
  org 0x7c00
BaseOfStack     equ   0x7c00
BaseOfLoader    equ   0x1000
OffsetOfLoader  equ   0x00

RootDirSectors            equ   14    ; 定义了根目录占用的扇区数
SectorNumOfRootDirStart   equ   19    ; 定义了根目录的起始扇区号
SectorNumOfFAT1Start      equ   1     ; FAT1 表的起始扇区号
SectorBalance             equ   17    ; 减去两个 FAT 表之后的起始扇区，数据区从 FAT[2] 开始

; 填写 FAT 引导扇区前 62 bytes 的数据
jmp	short start
nop
BS_OEMName	db	'MINEboot'
BPB_BytesPerSec	dw	512
BPB_SecPerClus	db	1
BPB_RsvdSecCnt	dw	1
BPB_NumFATs	db	2
BPB_RootEntCnt	dw	224
BPB_TotSec16	dw	2880
BPB_Media	db	0xf0
BPB_FATSz16	dw	9
BPB_SecPerTrk	dw	18
BPB_NumHeads	dw	2
BPB_HiddSec	dd	0
BPB_TotSec32	dd	0
BS_DrvNum	db	0
BS_Reserved1	db	0
BS_BootSig	db	0x29
BS_VolID	dd	0
BS_VolLab	db	'boot loader'
BS_FileSysType	db	'FAT12   '
```

> 由于引导扇区会被 BIOS 加载到物理内存 0x7c00，所以需要为把该段程序的起始地址设置为 0x7c00

在创建了 FAT12 的引导扇区数据之后，需要为引导程序准备读取软盘的功能以用来加载 `loader` 和 内核；主要是借助 BIOS 中断完成读取软盘的操作

```assembly
; 封装 BIOS 中断，采用 0x13 中断（功能号 ah = 0x02）实现软盘扇区的读取操作
; @AL: 读入的扇区数，@CH: 磁道号的低 8bit
; @CL: 扇区号 1~63(bit 0-5), 磁道号的高 2bit(bit 6-7，只对硬盘有效)
; @DH: 驱动器号，ES:BX 表示数据缓冲区
Func_ReadOneSector:
  ; Func_ReadOneSector 参数
  ; @AX: 待读取的磁盘起始扇区号 ; @CL: 读入的扇区数量 ; ES:BX 表示目标缓冲区起始地址
  ; 另外由于 BIOS 中断只能根据 磁道/磁头/扇区 来读取扇区，
  ; 因此需要将 Func_ReadOneSector 的 LBA 格式转换成 BIOS 的 CHS 格式，具体公示如下
  ; Q = LBA 扇区号 / 每磁道扇区数，R = LBA 扇区号 % 每磁道扇区数，磁道号 = Q >> 1，磁头号 = Q & 1，起始扇区号 = R + 1
  push  bp        ; 保存栈帧寄存器
  mov   bp,   sp  ; 开辟 2bytes 的栈上空间
  sub   esp,  2
  mov   byte  [bp - 2], cl      ; 保存读入的扇区数
  push  bx
  mov   bl,   [BPB_SecPerTrk]
  div   bl          ; al = ax / bl, ah = ax % bl
  inc   ah          ; 起始扇区号
  mov   cl,   ah    ; 按照上述公式计算出磁道号与磁头号，并保存在对应的寄存器
  mov   dh,   al
  shr   al,   1
  mov   ch,   al
  and   dh,   1
  pop   bx
  mov   dl,   [BS_DrvNum]
Label_Go_On_Reading:
  mov   ah,   2     ; 调用 BIOS 中断读取软盘扇区
  mov   al,   byte  [bp - 2]  ; [bp - 2] 表示读入的扇区数，在前面保存的
  int   13h
  jc    Label_Go_On_Reading
  add   esp,  2     ; 恢复栈
  pop   bp
  ret
```

在拥有了软盘读取功能之后，便可以在其基础上实现文件系统的访问功能。

1. 通过根目录的起始扇区号，并依据根目录占用的磁盘扇区数来确定需要搜索的扇区数，从根目录中每次读入一个扇区的数据到缓冲区
2. 遍历读入缓冲区的每个目录项（其中目录项的长度事先已经知道）
3. 匹配目录项中的文件名和带查找的文件名
   - 发现带查找的文件
     - 根据 FAT 表项提供的簇号顺序依次加载扇区数据到内存中
   - 没有发现带查找的文件
     - 显示日志信息

具体的实现代码见 `boot.asm::Label_Search_In_Root_Dir_Begin ~ boot.asm::Label_Go_On_Loading_File`

> 另外关于在找到文件对应的目录项之后如何读取文件数据到内存：目录项指出了文件所在的第一个簇号（由于在我们这里一个簇就是一个扇区，所以后面都用扇区），根据文件的第一个扇区可以计算出它在 FAT 表中的位置 这是因为在 FAT 表中第 n 项对应第 m 个扇区一开始就已经确定了的 不管该项存的是哪个文件的数据，FAT 表项记录了该文件的下一个扇区号，下一个扇区号又可以计算该扇区在 FAT 表中的位置，因此只需要顺序跟着读取就好了

最后只需要一条段间跳转指令跳转到 loader 程序即可

```assembly
Label_File_Loaded:
  jmp   BaseOfLoader:OffsetOfLoader
```

## loader

`loader` 必须在内核程序执行前，为其准备好一切所需的数据，包括

1. 检测硬件信息
   - BIOS 自检出的大部分信息只能在实模式下获取，而且内核运行于非实模式下，所以必须要在进入内核之前将这些信息检测出来，再作为参数供内核使用；硬件信息主要是通过 BIOS 中断服务程序来获取和检测
2. 处理器模式切换
   - `实模式` -> `保护模式` -> `IA-32e 模式（长模式）`，在各个模式切换的过程中 loader 引导加载程序必须手动创建各运行模式的临时数据，并按照标准流程执行模式间切换
3. 向内核传递参数等
   - 传递控制信息，例如启动模式是 字符界面 还是 图形界面
   - 传递硬件数据信息，例如内存起始地址和内存长度

```assembly
org   0x10000
  jmp Label_Start

%include "boot/fat12.inc"		; FAT12 的引导扇区数据

; 最终内核的加载地址
BaseOfKernelFile  equ   0x00
OffsetOfKernelFile  equ   0x100000

; 临时内核空间，因为内核是通过 BIOS 中断来加载的，
; BIOS 是工作在实模式下的，只能访问低于 1MB 的地址
; 所以必须先将内核读入临时空间，再通过特殊方式搬运到 1MB 以上的地址
BaseTmpOfKernelAddr equ   0x00
OffsetTmpOfKernelFile equ 0x7E00

; 在内核搬运到 1MB 以上的地址之后，原来临时存储内核的空间就可以拿来作为其他用途
; 这里拿来保存内存结构数据，供内核程序在初始化的时候使用
MemoryStructBufferAddr  equ 0x7E00
```

> 由于 loader 会被加载到 `0x10000` 内存处，所以其起始地址是 `0x10000`

内核首先会被加载到 `0x7E00` 然后会被搬运到 `0x100000`，在内核加载完毕之后 `0x7E00` 就用来保存内存结构信息

### 加载内核

内核会被加载到 `0x100000` 地址处，但是由于实模式下只能寻址 1MB 以内的地址空间，因此为了突破这一限制首先需要开启 1MB 以上物理地址寻址功能；1MB 以上的寻址由 A20 地址线管理，机器上电时默认情况下 A20 地址线是关闭的，可是使用下列方法开启

- 操作键盘控制器
- A20 快速门，使用 I/O 端口 `0x92` 来处理 A20 地址线
- BIOS 中断 `int 0x15` 的主功能号 `AX = 2401` 可以开启 A20 地址线，功能号 `AX = 2400` 可以禁用 A20 地址线
- 读取 `0xee` 端口开启 A20 地址线，写入该端口则会禁用 A20 地址线

```assembly
; ======= 开启 1MB 以上的物理地址寻址（打开 A20 地址线）
  ; 首先开启 A20 地址线
  push ax
  in al, 0x92
  or al, 0x02
  out 0x92, al
  pop ax

  cli   ; 关闭中断

  ; 加载 GDT
  db 0x66
  lgdt [GdtPtr]

  ; 设置 CR0 寄存器的第 0bit 开启保护模式
  mov eax, cr0
  or eax, 1
  mov cr0, eax

  ; 设置段寄存器，然后关闭保护模式，这样可以使得在实模式下寻址范围超过 1MB
  mov ax, SelectorData32
  mov fs, ax
  mov eax, cr0
  and al, 11111110b
  mov cr0, eax

  sti
```

上面这段代码先开启了保护模式，然后设置了 段寄存器，再关闭了保护模式；这并不是多此一举的操作，这是因为在保护模式下设置段寄存器的时候会同时设置段基址和段限长这两个隐藏部分，而我们的 cpu 是不管实模式和保护模式的，它只使用 `段基址 + 偏移` 访问内存地址

所以在经过上述操作之后，cpu 的寻址能力就变成了 4GB 而不再局限于 1MB，下图是经历上述过程之后各寄存器的信息；可以看到 `fs` 寄存器的 `base` 和 `limit` 和其他寄存器不一样

```shell
00014040193i[BIOS  ] Booting from 0000:7c00
^C00196624559i[      ] Ctrl-C detected in signal handler.
Next at t=196624560
(0) [0x0000000100ce] 1000:00ce (unk. ctxt): jmp .-3 (0x000100ce)      ; e9fdff
<bochs:2> sreg
es:0x1000, dh=0x00009301, dl=0x0000ffff, valid=1
    Data segment, base=0x00010000, limit=0x0000ffff, Read/Write, Accessed
cs:0x1000, dh=0x00009301, dl=0x0000ffff, valid=1
    Data segment, base=0x00010000, limit=0x0000ffff, Read/Write, Accessed
ss:0x0000, dh=0x00009300, dl=0x0000ffff, valid=7
    Data segment, base=0x00000000, limit=0x0000ffff, Read/Write, Accessed
ds:0x1000, dh=0x00009301, dl=0x0000ffff, valid=1
    Data segment, base=0x00010000, limit=0x0000ffff, Read/Write, Accessed
fs:0x0010, dh=0x00cf9300, dl=0x0000ffff, valid=1
    Data segment, base=0x00000000, limit=0xffffffff, Read/Write, Accessed
gs:0x0000, dh=0x00009300, dl=0x0000ffff, valid=1
    Data segment, base=0x00000000, limit=0x0000ffff, Read/Write, Accessed
ldtr:0x0000, dh=0x00008200, dl=0x0000ffff, valid=1
tr:0x0000, dh=0x00008b00, dl=0x0000ffff, valid=1
gdtr:base=0x0000000000010040, limit=0x17
idtr:base=0x0000000000000000, limit=0x3ff
```

在完成 cpu 寻址范围的提升之后，就可以加载内核了，读取内核到内存的方法和读取 `loader` 程序一样，只不过这个时候文件名变成内核文件名了，这里不再赘述

读取内核特殊的点在于，我们是先把内核读取到 `0x7E00` 地址处，然后再移动到 `0x100000`；这是因为读取内核代码到内存是 BIOS 中断服务实现的而 BIOS 在实模式下只支持上限为 1MB 的物理地址寻址，所以必须先将内核程序读取到临时空间再通过特殊方式搬运到 1MB 以上的内存空间；具体实现代码如下所示

```assembly
; ======= 读取 kernel.bin 文件数据到内存中
Label_FileName_Found:
  mov ax, RootDirSectors
  and di, 0xFFE0
  add di, 0x01A
  mov cx, word  [es:di]
  push cx
  add cx, ax
  add cx, SectorBalance
  mov eax, BaseTmpOfKernelAddr
  mov es, eax
  mov bx, OffsetTmpOfKernelFile
  mov ax, cx
; 循环读取，读取的过程和读取 loader.bin 大致相同
; 区别在于 kernel.bin 需要移动到 0x100000
Label_Go_On_Loading_File:
  push	ax
	push	bx
	mov	ah,	0Eh
	mov	al,	'.'
	mov	bl,	0Fh
	int	10h
	pop	bx
	pop	ax
  mov cl, 1
  call Func_ReadOneSector
  pop ax
; 读取数据到 0x7E00，并移动到 0x100000. Begin
  push cx
  push eax
  push fs
  push edi
  push ds
  push esi
  mov cx, 0x200   ; 后面的 loop 指令重复次数，0x200 = 512Byte，刚好是一个扇区的大小
  mov ax, BaseOfKernelFile
  mov fs, ax
  mov edi, dword  [OffsetOfKernelFileCount]
  mov ax, BaseTmpOfKernelAddr
  mov ds, ax
  mov esi, OffsetTmpOfKernelFile
; 移动内核，采用一个字节一个字节移动
Label_Mov_Kernel:
  mov al, byte  [ds:esi]
  mov byte  [fs:edi], al
  inc esi
  inc edi
  loop Label_Mov_Kernel
  mov eax, 0x1000
  mov ds, eax
  mov dword [OffsetOfKernelFileCount], edi
  pop esi
  pop ds
  pop edi
  pop fs
  pop eax
  pop cx
; 读取数据到 0x7E00，并移动到 0x100000. End
```

> 注意这段代码中 fs 寄存器在突破寻址范围之后又被重新赋值了，这在 bochs 虚拟机下没有问题，但是在物理机上重新赋值后寻址范围会变回正常范围

在完成内核程序加载之后软盘驱动器就可以关闭了

### 物理地址信息获取

借助 BIOS 中断 `int 0x15` 可以获取物理地址空间信息，获取到的信息保存在之前的内核临时空间中，之后操作系统会获取该地址处的内存结构数组并解析它

具体的获取方式比较简单，大致过程如下：

```assembly
;=======   get memory address size type

    ……

    mov    bp,    StartGetMemStructMessage
    int    10h

    mov    ebx,   0
    mov    ax,    0x00
    mov    es,    ax
    mov    di,    MemoryStructBufferAddr

Label_Get_Mem_Struct:
  mov eax, 0x0E820        ; 0x15 中断的功能号，表示获取内存信息
  mov ecx, 20             ; es:di 指向地址的结构体大小
  mov edx, 0x534D4150     ; 对系统将要要求的系统映像信息进行校验，信息被 BIOS 放置在 es:di 所指向的结构中
  int 0x15                ; 中断号
  jc Label_Get_Mem_Fail   ; 中断执行失败的时候会设置 CF 标志位
  add di, 20              ; 递增 di，保存下一个内存结构
  
  cmp ebx, 0              ; 中断返回的时候会设置下一个地址范围描述符的计数地址，第一次调用或者内存探测完毕的时候是 0
  jne Label_Get_Mem_Struct
  jmp Label_Get_Mem_OK
```

### 显示模式设置

这部本是关于 `VBE` 的显示模式，通过设置不同的显示模式号可以配置出不同的屏幕分辨率、每个像素的数据位宽、颜色格式等，这些信息都可以从 `bochs` 虚拟平台的 `SVGA` 芯片获取

| SVGA 显示模式 | 列   | 行   | 物理地址  | 像素点位宽 |
| :------------ | :--- | :--- | :-------- | :--------- |
| 0x180         | 1440 | 900  | e0000000h | 32 bit     |
| 0x143         | 800  | 600  | e0000000h | 32 bit     |

具体的实现大致过程如下：

1. `AH = 0x4F，BIOS 0x10` 获取 SVGA Information
2. 使用 BIOS 中断获取 SVGA mode 并调用 `DispAL` 显示十六进制信息数据
3. 使用 BIOS 中断设置 SVGA mode

设置好显示模式之后可以观察到 bochs 虚拟机的窗口尺寸会发生变化

### 实模式 -> 保护模式 -> IA-32e 模式

首先需要从实模式进入保护模式，进入保护模式的契机是 **置位 CR0 寄存器的 PE 标志位**；在进入保护模式之前处理器必须在模式切换前，在内存中创建一段可以在保护模式下执行的代码和必要的系统数据结构

- 系统数据结构；必须创建一个拥有代码段描述符和数据段描述符的 `GDT`，并使用 `LGDT` 指令将其加载到 `GDTR` 寄存器
  - 对于栈寄存器 ss 可以使用数据段，无需创建专用描述符
- 中断和异常；在保护模式下 中断/异常 都由 IDT 来管理
  - 在处理器切换到保护模式之前，引导加载程序已使用 `CLI` 指令关闭外部中断，所以在切换到保护模式的过程中不会产生中断和异常，因此不必完整的初始化 GDT，只要有相应的结构体即可
- 分页机制；如果需要分页，则必须在开启分页之前在内存中创建一个页目录和页表，并将页目录物理地址加载到 `CR3` 寄存器
- 多任务机制；如果希望使用多任务机制或允许改变特权级，则必须在首次任务切换之前创建至少一个 `TSS` 结构和 `TSS` 段描述符

我们这里创建了 GDT 和 IDT 相关的数据结构，保护模式并没有开启分页，这是因为等下的 `IA-32e` 模式需要关闭分页并重新设置页表

```assembly
; ======= GDT
LABEL_GDT:  dd 0, 0
LABEL_DESC_CODE32:  dd 0x0000FFFF, 0x00CF9A00
LABEL_DESC_DATA32:  dd 0x0000FFFF, 0x00CF9200

GdtLen  equ $ - LABEL_GDT
GdtPtr  dw  GdtLen - 1
        dd LABEL_GDT
SelectorCode32  equ LABEL_DESC_CODE32 - LABEL_GDT
SelectorData32  equ LABEL_DESC_DATA32 - LABEL_GDT

; ======= tmp IDT
IDT:
  times 0x50  dq  0
IDT_END:
IDT_POINTER:
  dw  IDT_END - IDT - 1
  dd  IDT
```

创建好相关内核数据结构之后，Intel 官方给出的保护模式切换过程如下，具体顺序可以有所不同

1. 执行 `CLI` 指令禁止可屏蔽硬件中断
2. 执行 `LGDT` 指令将 GDT 的基址和长度加载到 GDTR 寄存器
3. 执行 `mov cr0` 指令置位 CR0 寄存器的 PE 位（可以同时置位 PG 标志位用来开启分页）
   - 如果开启分页，那么该条指令和后续的 `JMP/CALL` 指令必须位于同一性地址映射的页面内
4. 执行一条远跳转或远调用指令，以切换到保护模式的代码段去执行
   - 这里有两个作用，第一个作用是切换到保护模式之后代码段寄存器还是实模式下的，所以执行跳转指令之后会重新加载代码段寄存器
   - 第二个作用是刷新 cpu 的流水线
5. 如需使用 LDT，借助 `LLDT` 将 GDT 内的 LDT 段选择子加载到 LDTR 寄存器
6. 执行 `LTR` 指令将一个 `TSS` 段描述符的段选择子加载到 TR 寄存器
7. 进入保护模式之后数据段寄存器依然保存着实模式的段数据，因此必须重新加载数据段寄存器
8. 执行 `LIDT` 指令，将保护模式下的 IDT 表的基地址和长度加载到 IDTR 寄存器
9. 最后执行 `STI` 指令使能可屏蔽硬件中断

具体的实现如下，由于没有使用 LDT 和 TSS 所以不需要过多的步骤；完成下面这些操作之后 cpu 就进入了保护模式

```assembly
; ======= 初始化 IDT GDT 并进入保护模式
  cli       ; 关闭中断，所以在切换到保护模式的过程中不会产生外部中断

  db 0x66
  lgdt [GdtPtr]   ; 设置 GDTR 寄存器

; db 0x66
; lidt [IDT_POINTER]

  ; 开启保护模式
  mov eax, cr0
  or eax, 1
  mov cr0, eax

  jmp dword SelectorCode32:GO_TO_TMP_Protect  ; 刷新代码段寄存器为保护模式，刷新 cpu 流水线
```

当处理器进入保护模式之后，紧接着需要开启 `IA-32e` 64bit 模式，同样的在进入该模式之前需要准备好执行代码、必要的系统数据结构（GDT、IDT）以及设置好相关寄存器。进入 IA-32e 模式的契机是 `IA32-EFER` 寄存器的第 8bit 即 `LME` 标志位置位

官方给出的 `IA-32e` 模式初始化步骤如下：

1. 关闭分页机制
2. 置位 `CR4` 的 `PAE` 标志位，开启物理地址扩展功能
3. 将页目录加载到 CR3 寄存器
4. 置位 `IA32_EFER` 寄存器的 `LME` 标志位，开启 `IA-32e` 模式
5. 置位 `CR0` 的 `PG` 标志位重新开启分页，此时处理器会自动置位 `IA32_ERER` 寄存器的 `LMA` 标志位

在保护模式下首要任务是初始化各个段寄存器以及栈指针，然后检测处理器是否支持 `IA-32e` 模式，如果支持则开始向 `IA-32e` 模式切换，检测代码如下所示

```assembly
GO_TO_TMP_Protect:
; ======= go to tmp long mode
  mov ax, 0x10      ; 重新加载数据段寄存器，使其从实模式变成保护模式
  mov ds, ax
  mov es, ax
  mov fs, ax
  mov ss, ax
  mov esp, 0x7E00

  call support_long_mode
  test eax, eax           ; 对操作数进行 and 运算
  jz no_support           ; ZF(zero) 标志被置位则跳转

; ======= test support long mode or not
support_long_mode:
  ; 确定 cpu 支持的最大扩展功能号，返回结果也保存在 eax 中
  mov eax, 0x80000000
  cpuid
  cmp eax, 0x80000001         ; eax < 0x80000001 CF = 1，eax >= 0x80000001 CF = 0
  setnb al                    ; setnb 指令用于根据进位标志 CF 设置操作数 al。如果 CF = 1 则 al = 0，否则 al = 1
  jb support_long_mode_done   ; CF = 1 说明不支持 0x80000001 扩展功能，则跳转
  mov eax, 0x80000001
  cpuid
  bt edx, 29                  ; 把 edx bit29 传递给 CF 标志位，=1 说明处理器支持 IA-32e 模式
  setc al                     ; CF = 1 -> al = 1
support_long_mode_done:
  movzx eax, al
  ret
; ======= no support
no_support:
  jmp $
```

> 这里采用 CPUID 指令首先检测 cpu 所支持的最大扩展功能号，如果支持 0x80000001 的话 再采用该扩展功能执行 CPUID 指令，检测执行结果的 bit 29 如果是 1 就说明处理器支持 IA-32e 模式

完成了处理器模式检测之后，下一步需要为 IA-32e 模式配置 PAE 临时页目录页页表（PAE 模式采用三级页表），临时页表采用硬编码的方式，真正的页表会在内核代码里面重新设置

```assembly
; ======= init temporary page table 0x90000
  ; 这里的页表是 IA-32e 模式的页表，页表项大小为 8B
  
  ; PML4
  mov	dword	[0x90000],	0x91007   ; 映射 0 地址开始的地址
	mov	dword	[0x90004],	0x00000
	mov	dword	[0x90800],	0x91007
	mov	dword	[0x90804],	0x00000

  ; PDPT
	mov	dword	[0x91000],	0x92007
	mov	dword	[0x91004],	0x00000

  ; PDT
	mov	dword	[0x92000],	0x000083
	mov	dword	[0x92004],	0x000000

	mov	dword	[0x92008],	0x200083
	mov	dword	[0x9200c],	0x000000

	mov	dword	[0x92010],	0x400083
	mov	dword	[0x92014],	0x000000

	mov	dword	[0x92018],	0x600083
	mov	dword	[0x9201c],	0x000000

	mov	dword	[0x92020],	0x800083
	mov	dword	[0x92024],	0x000000

	mov	dword	[0x92028],	0xa00083
	mov	dword	[0x9202c],	0x000000

  ; 后面不需要 PT 了，这是因为在 PDTE 里面的第 7bit 置为 1 了，
  ; 说明采用了 2MB 的页大小，所以不需要下一级页表了
```

随后只需要加载 GDT、打开 PAE（cr4 寄存器的第 5 位）、设置 CR3 寄存器为页目录的物理地址，最后打开 IA-32e 模式、开启分页功能，至此就完成了 IA-32e 模式的切换；最后只需要执行一条跨段 跳转/调用 指令，将 CS 段寄存器更新为 IA-32e 模式的代码段描述符

```assembly
; ======= enable long-mode
  mov ecx, 0x0C0000080
  rdmsr				; IA32_EFER 寄存器位于 MSR 寄存器组，使用 rdmsr/wrmsr 可以访问 64bit 的 MSR 寄存器
  						; EDX 存储 MSR 的高 32bit，EAX 存储 MSR 的低 32bit
  bts eax, 8
  wrmsr
```

## 补充

### 页表

> 关于程序中的页表，`IA-32e` 模式的页表可以看作 PAE 页表的扩充，将 `PDPT` 从 4 个表项扩展成 512 个，然后额外添加一个 `level-4` 页表 `PML4`

正常的 64bit 模式下的页表是有 4 级页表的，分别是 `PML4`, `PDPT`, `PDT`, `PT`，但是在我们这里没有 `PT` 页表了，这是因为如果在 `PDT` 页表的页表项中将第 7 bit 置位了那就说明开启了 `2MB` 的页大小；`2M = 2^21, 21 = 9 + 12` 刚好不需要 `PT` 页表索引的 9bit 了，所以也就不需要 `PT` 页表了 ；所以上述页表对于 `loader` 程序的地址 `0x1????` 是完全可以正常访问的，因为`PML4`, `PDPT`, `PDT` 中的偏移全是 0，也就是全部对应第一个表项，最后 `0x1????` 地址对应的物理地址在 `PDT` 第一个 `entry` 表示的物理页内

另外 对于上述 `PDT` 表的表项，相邻的两个表项地址相差 `0x200000`，因为 `page size = 2MB = 2^21`，所以相差 `0x200000` 也就相差了一个 page

## 参考

[指令集文档](https://faydoc.tripod.com/cpu/setnb.htm)；

SVGA：[DOS下的SVGA编程_u011423824的专栏-程序员宅基地 - 程序员宅基地 (cxyzjd.com)](http://www.cxyzjd.com/article/u011423824/45360119)；[INT 10, AH=4F - 极客分享 (geek-share.com)](https://www.geek-share.com/detail/2492944880.html)；

PAE：[linux内存管理之PAE（物理地址扩展）解决内存大于4G的问题_jinking01的专栏-CSDN博客](https://blog.csdn.net/jinking01/article/details/105834801)

