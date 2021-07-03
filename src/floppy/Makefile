floppy:
	dd if=/dev/zero of=boot.img bs=512 count=2880 conv=notrunc
	dd if=../boot/boot of=boot.img bs=512 count=2880 conv=notrunc
bochs:
	bochs -f bochsrc