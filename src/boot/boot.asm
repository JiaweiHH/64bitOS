  org 0x7c00
BaseOfStack equ 0x7c00

start:
  mov ax, cs
  mov ds, ax
  mov es, ax
  mov ss, ax
  mov sp, BaseOfStack

  ; 使用 BIOS 中断 0x10 功能号 ah = 06h，清空屏幕
  mov ax, 0600h
  mov bx, 0700h
  mov cx, 0
  mov dx, 0184fh
  int 10h

  ; 使用 BIOS 中断 0x10 功能号 ah = 02h，设置光标
  mov ax, 0200h
  mov bx, 0000h
  mov dx, 0000h
  int 10h

  ; 显示屏幕信息：Start Booting......，BIOS 中断 0x10 功能号 ah = 13h
  mov ax, 1301h
  mov bx, 000fh
  mov dx, 0000h
  mov cx, 10
  push ax
  mov ax, dx
  mov es, ax
  pop ax
  mov bp, StartBootMessage
  int 10h
  
  ; 重置软盘
  xor	ah,	ah
	xor	dl,	dl
	int	13h
	jmp	$

StartBootMessage: db "Start Boot"

  ; 填充第一个扇区的字节
  times 510 - ($ - $$) db 0
  dw    0xaa55