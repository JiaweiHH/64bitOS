org   0x10000
  jmp Label_Start

%include "boot/fat12.inc"

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

[SECTION gdt]
LABEL_GDT:  dd 0, 0
LABEL_DESC_CODE32:  dd 0x0000FFFF, 0x00CF9A00
LABEL_DESC_DATA32:  dd 0x0000FFFF, 0x00CF9200

GdtLen  equ $ - LABEL_GDT
GdtPtr  dw  GdtLen - 1
        dd LABEL_GDT

SelectorCode32  equ LABEL_DESC_CODE32 - LABEL_GDT
SelectorData32  equ LABEL_DESC_DATA32 - LABEL_GDT

[SECTION gdt64]
LABEL_GDT64:  dq  0x0000000000000000
LABEL_DESC_CODE64:  dq  0x0020980000000000
LABEL_DESC_DATA64:  dq  0x0000920000000000

GdtLen64  equ $ - LABEL_GDT64
GdtPtr64  dw  GdtLen64 - 1
          dd LABEL_GDT64

SelectorCode64  equ LABEL_DESC_CODE64 - LABEL_GDT64
SelectorData64  equ LABEL_DESC_DATA64 - LABEL_GDT64

[SECTION .s16]
[BITS 16]
Label_Start:
  mov ax, cs
  mov ds, ax
  mov es, ax
  mov ax, 0x00
  mov ss, ax
  mov sp, 0x7c00

  ; ======= 屏幕显示: Start Loader......
  mov ax, 0x1301
  mov bx, 0x000f
  mov dx, 0x0200
  mov cx, 12
  push ax
  mov ax, ds
  mov es, ax
  pop ax
  mov bp, StartLoaderMessage
  int 10h

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

;=======	reset floppy

	xor	ah,	ah
	xor	dl,	dl
	int	13h

; ======= search kernel.bin，过程和 boot.asm 中查找 loader.bin 差不多
  mov word  [SectorNo], SectorNumOfRootDirStart

Lable_Search_In_Root_Dir_Begin:

	cmp	word	[RootDirSizeForLoop],	0
	jz	Label_No_LoaderBin
	dec	word	[RootDirSizeForLoop]	
	mov	ax,	00h
	mov	es,	ax
	mov	bx,	8000h
	mov	ax,	[SectorNo]
	mov	cl,	1
	call	Func_ReadOneSector
	mov	si,	KernelFileName
	mov	di,	8000h
	cld
	mov	dx,	10h
	
Label_Search_For_LoaderBin:

	cmp	dx,	0
	jz	Label_Goto_Next_Sector_In_Root_Dir
	dec	dx
	mov	cx,	11

Label_Cmp_FileName:

	cmp	cx,	0
	jz	Label_FileName_Found
	dec	cx
	lodsb	
	cmp	al,	byte	[es:di]
	jz	Label_Go_On
	jmp	Label_Different

Label_Go_On:
	
	inc	di
	jmp	Label_Cmp_FileName

Label_Different:

	and	di,	0FFE0h
	add	di,	20h
	mov	si,	KernelFileName
	jmp	Label_Search_For_LoaderBin

Label_Goto_Next_Sector_In_Root_Dir:
	
	add	word	[SectorNo],	1
	jmp	Lable_Search_In_Root_Dir_Begin

Label_No_LoaderBin:

	mov	ax,	1301h
	mov	bx,	008Ch
	mov	dx,	0300h		;row 3
	mov	cx,	21
	push	ax
	mov	ax,	ds
	mov	es,	ax
	pop	ax
	mov	bp,	NoLoaderMessage
	int	10h
	jmp	$

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
; 区别在于 kernel.bin 是先读取到 0x7E00 在移动到 0x100000
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

  ; 下一簇数据
  call Func_GetFATEntry
  cmp	ax,	0FFFh
	jz	Label_File_Loaded
	push	ax
	mov	dx,	RootDirSectors
	add	ax,	dx
	add	ax,	SectorBalance

	jmp	Label_Go_On_Loading_File

Label_File_Loaded:
  mov ax, 0xB800
  mov gs, ax
  mov ah, 0x0F    ; 0000: 黑底，1111: 白字
  mov al, 'G'
  mov [gs:((80 * 0 + 39) * 2)], ax    ; 屏幕第 0 行，第 39 列

; Loader 引导加载程序完成内核的加载之后，软盘驱动器不再需要使用
; 下面的代码用来关闭软盘驱动器
KillMotor:
  push dx
  mov dx, 0x03F2
  mov al, 0
  out dx, al
  pop dx

; ======= 由于内核不再使用原来的临时空间，该空间将用来保存物理内存信息
; ======= 下面的代码是物理地址空间的获取过程
  mov ax, 0x1301
  mov bx, 0x000F
  mov dx, 0x0400
  mov cx, 24
  push ax
  mov ax, ds
  mov es, ax
  pop ax
  
  mov bp, StartGetMemStructMessage  ; 首先在屏幕上显示日志信息
  int 0x10

  mov ebx, 0
  mov ax, 0x00
  mov es, ax
  mov di, MemoryStructBufferAddr

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

Label_Get_Mem_Fail:       ; 显示日志信息，es:bp 指向了要显示的字符串的内存地址
  mov	ax,	0x1301
	mov	bx,	0x008C
	mov	dx,	0x0500		;row 5
	mov	cx,	23
	push	ax
	mov	ax,	ds
	mov	es,	ax
	pop	ax
	mov	bp,	GetMemStructErrMessage
	int	0x10
	jmp	$

Label_Get_Mem_OK:
  mov ax, 0x1301
  mov bx, 0x000F
  mov dx, 0x0600
  mov cx, 29
  push ax
  mov ax, ds
  mov es, ax
  pop ax
  mov bp, GetMemStructOKMessage
  int 0x10    ; 显示日志信息

; ======= get SVGA information
  mov ax, 0x1301
  mov bx, 0x000F
  mov dx, 0x0800
  mov cx, 23
  push ax
  mov ax, ds
  mov es, ax
  pop ax
  mov bp, StartGetSVGAVBEInfoMessage
  int 0x10    ; 先显示日志信息

  mov ax, 0x00
  mov es, ax
  mov di, 0x8000
  mov ax, 0x4F00
  int 0x10    ; ah = 0x4F，BIOS 0x10 中断会获取 SVGA Information

  cmp ax, 0x004F  ; BIOS 0x10 ah=0x4F 调用成功的时候，会设置返回值 ax = 0x004F
  jz .KO

; ======= BIOS 获取 SVGA 信息失败
  mov ax, 0x1301
  mov bx, 0x008C
  mov dx, 0x0900
  mov cx, 23
  push ax
  mov ax, ds
  mov es, ax
  pop ax
  mov bp, StartGetSVGAVBEInfoMessage
  int 0x10    ; 显示日志信息

  jmp $

.KO:
  mov	ax,	0x1301
	mov	bx,	0x000F
	mov	dx,	0x0A00		;row 10
	mov	cx,	29
	push	ax
	mov	ax,	ds
	mov	es,	ax
	pop	ax
	mov	bp,	GetSVGAVBEInfoOKMessage
	int	0x10    ; 显示日志信息

; ======= get SVGA mode info
  mov	ax,	1301h
	mov	bx,	000Fh
	mov	dx,	0C00h		;row 12
	mov	cx,	24
	push	ax
	mov	ax,	ds
	mov	es,	ax
	pop	ax
	mov	bp,	StartGetSVGAModeInfoMessage
	int	10h   ; 显示日志信息

  mov ax, 0x00
  mov es, ax
  mov si, 0x800E

  mov esi, dword [es:si]
  mov edi, 0x8200         ; 0x10 ah = 0x4F，用于保存获取的信息的内存地址

Label_SVGA_Mode_Info_Get:
  mov cx, word [es:esi]   ; 显示内容保存在 cx 寄存器中

; ======= display SVGA mode information，显示 cx 寄存器的内存，分两次显示 先显示高 bit 再显示低 bit
  push ax
  mov ax, 0x00
  mov al, ch
  call Label_DispAL

  mov ax, 0x00
  mov al, cl
  call Label_DispAL

  pop ax

; =======
  cmp cx, 0x0FFFF
  jz Label_SVGA_Mode_Info_Finish

  mov ax, 0x4F01      ; GET SuperVGA MODE INFORMATION  
  int 0x10
  cmp ax, 0x004F
  jnz Label_SVGA_Mode_Info_FAIL

  add esi, 2
  add edi, 0x100

  jmp Label_SVGA_Mode_Info_Get

Label_SVGA_Mode_Info_FAIL:

	mov	ax,	0x1301
	mov	bx,	0x008C
	mov	dx,	0x0D00		;row 13
	mov	cx,	24
	push	ax
	mov	ax,	ds
	mov	es,	ax
	pop	ax
	mov	bp,	GetSVGAModeInfoErrMessage
	int	0x10

Label_SET_SVGA_Mode_VESA_VBE_FAIL:

	jmp	$

Label_SVGA_Mode_Info_Finish:

	mov	ax,	0x1301
	mov	bx,	0x000F
	mov	dx,	0x0E00		;row 14
	mov	cx,	30
	push	ax
	mov	ax,	ds
	mov	es,	ax
	pop	ax
	mov	bp,	GetSVGAModeInfoOKMessage
	int	0x10

; ======= set the SVGA mode (VESA VBE)
  mov ax, 0x4F02    ; SET SuperVGA VIDEO MODE  
  mov bx, 0x4180    ; mode: 0x180 or 0x143
  int 0x10

  cmp ax, 0x004F
  jnz Label_SET_SVGA_Mode_VESA_VBE_FAIL

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

[SECTION .s32]
[BITS 32]
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

  ; 后面不需要 PT 了，这是因为在 PDTE 里面的第 7bit 置为 1了，
  ; 说明采用了 2MB 的页大小，所以不需要下一级页表了

; ======= load GDTR
  db 0x66
  lgdt [GdtPtr64]
  mov ax, 0x10
  mov ds, ax
  mov es, ax
  mov fs, ax
  mov gs, ax
  mov ss, ax

  mov esp, 0x7E00

; ======= open PAE
  mov eax, cr4
  bts eax, 5
  mov cr4, eax

; ======= load cr3
  mov eax, 0x90000
  mov cr3, eax

; ======= enable long-mode
  mov ecx, 0x0C0000080
  rdmsr                 ; IA32_EFER 寄存器位于 MSR 寄存器组，使用 rdmsr/wrmsr 可以访问 64bit 的 MSR 寄存器
                        ; EDX 存储 MSR 的高 32bit，EAX 存储 MSR 的低 32bit
  bts eax, 8
  wrmsr

; ======= open PE and paging
  mov eax, cr0
  bts eax, 0
  bts eax, 31
  mov cr0, eax

  jmp SelectorCode64:OffsetOfKernelFile


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

; ======= read one sector from floppy
[SECTION .s16lib]
[BITS 16]

Func_ReadOneSector:
	
	push	bp
	mov	bp,	sp
	sub	esp,	2
	mov	byte	[bp - 2],	cl
	push	bx
	mov	bl,	[BPB_SecPerTrk]
	div	bl
	inc	ah
	mov	cl,	ah
	mov	dh,	al
	shr	al,	1
	mov	ch,	al
	and	dh,	1
	pop	bx
	mov	dl,	[BS_DrvNum]
Label_Go_On_Reading:
	mov	ah,	2
	mov	al,	byte	[bp - 2]
	int	13h
	jc	Label_Go_On_Reading
	add	esp,	2
	pop	bp
	ret

;=======	get FAT Entry

Func_GetFATEntry:

	push	es
	push	bx
	push	ax
	mov	ax,	00
	mov	es,	ax
	pop	ax
	mov	byte	[Odd],	0
	mov	bx,	3
	mul	bx
	mov	bx,	2
	div	bx
	cmp	dx,	0
	jz	Label_Even
	mov	byte	[Odd],	1

Label_Even:

	xor	dx,	dx
	mov	bx,	[BPB_BytesPerSec]
	div	bx
	push	dx
	mov	bx,	8000h
	add	ax,	SectorNumOfFAT1Start
	mov	cl,	2
	call	Func_ReadOneSector
	
	pop	dx
	add	bx,	dx
	mov	ax,	[es:bx]
	cmp	byte	[Odd],	1
	jnz	Label_Even_2
	shr	ax,	4

Label_Even_2:
	and	ax,	0FFFh
	pop	bx
	pop	es
	ret

; ======= display num in al
[SECTION .s16lib]
[BITS 16]
; @AL: 要显示的十六进制数
Label_DispAL:
  push ecx
  push edx
  push edi

  mov edi, [DisplayPosition]  ; 屏幕偏移值
  mov ah, 0x0F                ; 字体的颜色属性
  
  ; 先显示 AL 寄存器的高 4bit
  mov dl, al
  shr al, 4
  mov ecx, 2    ; loop 指令的重复次数
.begin:
  and al, 0x0F
  cmp al, 9
  ja .1         ; 大于 9 的时候，十六进制从 A 开始
  add al, '0'   ; 小于等于 9，则直接与 '0' 相加，结果就是十六进制的答案
  jmp .2
.1:
  sub al, 0x0A
  add al, 'A'
.2:
  mov [gs:edi], ax    ; 显示 AL 寄存器的高 4bit
  add edi, 2          ; 由于一个字符占 2 byte，所以 +2
  
  mov al, dl          ; loop 显示 AL 寄存器的低 4bit
  loop .begin

  mov [DisplayPosition],  edi
  
  pop edi
  pop edx
  pop ecx

  ret


; ======= tmp IDT
IDT:
  times 0x50  dq  0
IDT_END:
IDT_POINTER:
  dw  IDT_END - IDT - 1
  dd  IDT

;=======	tmp variable

RootDirSizeForLoop	dw	RootDirSectors
SectorNo		dw	0
Odd			db	0
OffsetOfKernelFileCount	dd	OffsetOfKernelFile

DisplayPosition		dd	0

;=======	display messages

StartLoaderMessage:	db	"Start Loader"
NoLoaderMessage:	db	"ERROR:No KERNEL Found"
KernelFileName:		db	"KERNEL  BIN", 0
StartGetMemStructMessage:	db	"Start Get Memory Struct."
GetMemStructErrMessage:	db	"Get Memory Struct ERROR"
GetMemStructOKMessage:	db	"Get Memory Struct SUCCESSFUL!"

StartGetSVGAVBEInfoMessage:	db	"Start Get SVGA VBE Info"
GetSVGAVBEInfoErrMessage:	db	"Get SVGA VBE Info ERROR"
GetSVGAVBEInfoOKMessage:	db	"Get SVGA VBE Info SUCCESSFUL!"

StartGetSVGAModeInfoMessage:	db	"Start Get SVGA Mode Info"
GetSVGAModeInfoErrMessage:	db	"Get SVGA Mode Info ERROR"
GetSVGAModeInfoOKMessage:	db	"Get SVGA Mode Info SUCCESSFUL!"