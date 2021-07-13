C_SOURCES = $(shell find . -name "*.c")
C_OBJECTS = $(patsubst %.c, %.o, $(C_SOURCES))
S_SOURCES = $(shell find . -name "*.S")
S_OBJECTS = $(patsubst %.S, %.o, $(S_SOURCES))
BOOT_SOURCES	= $(shell find . -name "*.asm")
BOOT_OBJECTS = $(patsubst %.asm, %.bin, $(BOOT_SOURCES))


ASM = nasm
CFLAGES := -mcmodel=large -fno-builtin -m64 -fno-stack-protector -O2 -c -I include

# 会出现重复定义的行为，使用链接器的 -z muldefs 参数表示当出现重复定义的时候只使用其中的一个
LDFLAGS := -b elf64-x86-64 -z muldefs -T scripts/kernel.lds

.PHONY: update_image mount_image umount_image clean clear_image bochs
all: system

%.bin: %.asm
	@echo 编译 boot 文件 $< ...
	$(ASM) $< -o $@

%.o: %.S
	as --64 -o $@ $<

%.o: %.c
	gcc $(CFLAGES) -o $@ $<

system: $(S_OBJECTS) $(C_OBJECTS)
	ld $(LDFLAGS) -o $@ $^

bochs: update_image
	bochs -f tools/bochsrc

clear_image: $(BOOT_OBJECTS)
	dd if=/dev/zero of=boot.img bs=512 count=2880 conv=notrunc
	dd if=./boot/boot.bin of=boot.img bs=512 count=2880 conv=notrunc
	sudo mount boot.img ./mnt -t vfat -o loop
	sudo cp ./boot/loader.bin ./mnt
	sudo sync
	sudo umount ./mnt

update_image:
	objcopy -I elf64-x86-64 -S -R ".eh_frame" -R ".comment" -O binary system kernel.bin
	sudo mount boot.img ./mnt -t vfat -o loop
	sudo cp kernel.bin ./mnt
	sudo sync
	sudo umount ./mnt

mount_image:
	sudo mount boot.img ./mnt -t vfat -o loop

umount_image:
	sudo umount ./mnt

clean:
	rm -rf $(BOOT_OBJECTS) $(S_OBJECTS) $(C_OBJECTS) system kernel.bin