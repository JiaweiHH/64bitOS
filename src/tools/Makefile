create_floppy:
	dd if=/dev/zero of=boot.img bs=512 count=2880 conv=notrunc
	dd if=../boot/boot.bin of=boot.img bs=512 count=2880 conv=notrunc

update_img:
	sudo mount boot.img ../mnt -t vfat -o loop
	sudo cp ../boot/loader.bin ../mnt
	sudo cp ../boot/kernel.bin ../mnt
	sudo sync
	sudo umount ../mnt

bochs:
	bochs -f bochsrc