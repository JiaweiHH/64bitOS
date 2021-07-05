S_SOURCES = $(shell find . -name "*.asm")
S_OBJECTS = $(patsubst %.asm, %.o, $(S_SOURCES))
S_BINS		= $(patsubst %.asm, %.bin, $(S_SOURCES))

ASM = nasm

.PHONY: bochs clean
all: update_image bochs

%.bin: %.asm
	@echo 编译 bootloader 文件
	$(ASM) $< -o $@

new_image: $(S_BINS)
	make -C $(shell pwd)/floppy

update_image:
	sudo mount floppy/boot.img mnt/ -t vfat -o loop
	sudo cp boot/loader.bin mnt
	sudo sync
	sudo umount mnt/

bochs:
	make -C $(shell pwd)/floppy bochs

clean:
	rm $(S_BINS)