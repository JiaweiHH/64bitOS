all: $(S_BINS)
%.asm: %.bin
	@echo 编译 bootloader 文件
	$(ASM) $< -o $@
