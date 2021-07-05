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

start:
  mov   ax, cs
  mov   ds, ax
  mov   es, ax
  mov   ss, ax
  mov   sp, BaseOfStack

  ; 使用 BIOS 中断 0x10 功能号 ah = 06h，清空屏幕
  mov   ax, 0600h
  mov   bx, 0700h
  mov   cx, 0
  mov   dx, 0184fh
  int   10h

  ; 使用 BIOS 中断 0x10 功能号 ah = 02h，设置光标
  mov   ax, 0200h
  mov   bx, 0000h
  mov   dx, 0000h
  int   10h

  ; 显示屏幕信息：Start Booting......，BIOS 中断 0x10 功能号 ah = 13h
  mov   ax, 1301h
  mov   bx, 000fh
  mov   dx, 0000h
  mov   cx, 10
  push  ax
  mov   ax, ds
  mov   es, ax
  pop   ax
  mov   bp, StartBootMessage
  int   10h
  
  ; 重置软盘
  xor	ah,	ah
	xor	dl,	dl
	int	13h

  ; 查找 loader.bin
  ; 两层循环，第一层是循环根目录的所有扇区
  ; 第二层是在扇区内循环查找所有的目录项
  mov   word  [SectorNo], SectorNumOfRootDirStart
Label_Search_In_Root_Dir_Begin:
  cmp   word  [RootDirSizeForLoop], 0   ; 根据根目录的大小，决定循环的次数
  jz    Label_No_LoaderBin
  dec   word  [RootDirSizeForLoop]
  mov   ax,   00h         ; 设置好相关参数，调用 Func_ReadOneSector
  mov   es,   ax
  mov   bx,   8000h
  mov   ax,   [SectorNo]
  mov   cl,   1
  call  Func_ReadOneSector
  mov   si,   LoaderFileName
  mov   di,   8000h
  cld                       ; LODSB 指令需要用到 DF 标志位，所以需要清空 DF 标志位
  mov   dx,   10h           ; 记录每个扇区可容纳的目录项个数，512 / 32 = 0x10，dx 表示在扇区内的循环次数

Label_Search_For_LoaderBin:
  cmp   dx,   0
  jz    Label_Goto_Next_Sector_In_Root_Dir    ; 该扇区不可读？寻找下一个扇区
  dec   dx
  mov   cx,   11            ; 记录目录项的文件名长度

Label_Cmp_FileName:
  cmp   cx,   0               ; 检查文件名长度是不是 0，当长度是 0 的时候说明已经找到想要的文件了
  jz    Label_FileName_Found
  dec   cx                    ; 文件名长度减一，并且下面会读取一个字节的文件名数据进行比较
  lodsb                       ; 在 DS:SI 内存中读取一个字节的数据到 AX 寄存器中
  cmp   al,   byte  [es:di]   ; 逐字节比较
  jz    Label_Go_On           ; 如果相等
  jmp   Label_Different       ; 如果不相等

Label_Go_On:
  inc   di                    ; 指向下一个字节的内存地址
  jmp   Label_Cmp_FileName    ; 继续比较

Label_Different:
  and   di,   0ffe0h          ; 将目录项对其到其开始位置
  add   di,   20h             ; 增加 32bytes 到下一个目录项
  mov   si,   LoaderFileName
  jmp   Label_Search_For_LoaderBin  ; 检查下一个目录项

Label_Goto_Next_Sector_In_Root_Dir:
  add   word  [SectorNo],   1           ; SectorNo + 1
  jmp   Label_Search_In_Root_Dir_Begin

; 显示 NoLoaderMessage 字符信息
Label_No_LoaderBin:
  mov   ax,   1301h
  mov   bx,   008ch
  mov   dx,   0100h
  mov   cx,   21
  push  ax
  mov   ax,   ds
  mov   es,   ax
  pop   ax
  mov   bp,   NoLoaderMessage
  int   10h
  jmp   $

; 找到了 loader 程序
Label_FileName_Found: 
  mov   ax,   RootDirSectors
  and   di,   0ffe0h          ; 对齐到 32bit
  add   di,   01ah            ; DIR_FstClus 字段在目录项中的偏移是 0x1A
  mov   cx,   word  [es:di]   ; 读取目录项的起始簇号，也就是 FAT 表项
  push  cx                    ; 保存起始簇号
  add   cx,   ax              ; 从这里开始的三条指令计算实际的扇区号
  add   cx,   SectorBalance
  mov   ax,   BaseOfLoader    ; 这里三行开始设置内存中的加载地址 [es:bx]
  mov   es,   ax
  mov   bx,   OffsetOfLoader
  mov   ax,   cx
; 从目录项指引出的第一个簇号开始，加载文件扇区到内存中
Label_Go_On_Loading_File:
  push  ax                    ; 保存 ax 和 bx
  push  bx
  mov   ah,   0eh             ; 显示字符 .
  mov   al,   '.'
  mov   bl,   0fh
  int   10h
  pop   bx
  pop   ax                    ; ax 保存着 loader 的扇区，bx 保存着内存中的偏移

  mov   cl,   1               
  call  Func_ReadOneSector    ; 读取一个扇区的数据
  pop   ax
  call  Func_GetFATEntry      ; 读取 FAT 表项
  cmp   ax,   0fffh           ; 到达文件的最后一个簇
  jz    Label_File_Loaded
  push  ax                    ; 如果不是文件的最后一个簇，那么此时的 ax 表示下一个簇号，
                              ; 等待下一次循环再次读取的 FAT 表项的时候会用到
  mov   dx,   RootDirSectors
  add   ax,   dx
  add   ax,   SectorBalance         ; 读取 loader 的下一个扇区
  add   bx,   [BPB_BytesPerSec]     ; 内存偏移增加一个扇区大小
  jmp   Label_Go_On_Loading_File
Label_File_Loaded:
  jmp   BaseOfLoader:OffsetOfLoader


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


; 读取 FAT 表项，@ax 表示 FAT 表项号
; FAT 表项里面会指引处文件的下一个簇号
Func_GetFATEntry:
  push  es
  push  bx
  push  ax
  mov   ax,   00
  mov   es,   ax
  pop   ax
  mov   byte  [Odd],  0
  mov   bx,   3           ; 下面的代码表示将 FAT 表项乘以 1.5B（因为每个 FAT 表项大小为 1.5B）
  mul   bx
  mov   bx,   2
  div   bx
  cmp   dx,   0
  jz    Label_Even        ; 下一个表项的位置是 1.5B 的整数倍
  mov   byte  [Odd],  1   ; 如果不是 1.5B 的整数倍，先设置 Odd = 1
Label_Even:
  xor   dx,   dx
  mov   bx,   [BPB_BytesPerSec]
  div   bx                ; 除以每扇区字节数来获取 FAT 表项的偏移扇区号，保存在 ax
  push  dx
  mov   bx,   8000h       ; 下面调用 Func_ReadOneSector 读取两个扇区的数据
                          ; 这是为了解决 FAT 表项横跨两个扇区的问题
  add   ax,   SectorNumOfFAT1Start
  mov   cl,   2
  call  Func_ReadOneSector

  pop   dx                ; dx 保存了余数，也就是扇区内的偏移
  add   bx,   dx          ; 地址增加偏移大小
  mov   ax,   [es:bx]     ; 读取数据，ax 是 16bit，所以读取 16bit
  cmp   byte  [Odd],  1
  jnz   Label_Even_2
  shr   ax,   4           ; ax >> 4，如果是奇数项不需要后面的 4bit
Label_Even_2:
  and   ax,   0fffh       ; 针对 ax >> 4，如果是偶数项不需要前面的 4bit
  pop   bx
  pop   es
  ret

; 下面三个变量保存运行时的临时数据
RootDirSizeForLoop  dw  RootDirSectors
SectorNo            dw  0
Odd                 db  0

; 下面的变量显示日志信息
StartBootMessage:   db  "Start Boot"
NoLoaderMessage:    db  "ERROR:No LOADER Found"
LoaderFileName:     db  "LOADER  BIN", 0

; 填充第一个扇区的字节
  times 510 - ($ - $$) db 0
  dw    0xaa55