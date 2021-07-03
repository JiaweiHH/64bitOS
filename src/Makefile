S_SOURCES = $(shell find . -name "*.asm")
S_OBJECTS = $(patsubst %.asm, %.o, $(S_SOURCES))

CC = gcc
LD = ld
ASM = nasm

.PHONY: bochs clean

bochs:
	make -C $(shell pwd)/floppy floppy
	make -C $(shell pwd)/floppy bochs