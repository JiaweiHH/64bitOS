all: $(S_BINS)
%.bin: %.asm
	@echo ========== 编译 bootloader 文件 ==========
	$(ASM) $< -o $@
