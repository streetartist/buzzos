BUILD := build
BUILD_ID := $(shell powershell -NoProfile -Command "[DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()")
OBJDIR := $(BUILD)/obj/$(BUILD_ID)
IMAGE := $(BUILD)/buzzos.img
KERNEL_SECTORS := 64

NASM := nasm
CC := clang
LD := ld.lld
OBJCOPY := llvm-objcopy

CFLAGS := --target=i386-none-elf -std=c11 -ffreestanding -fno-builtin \
	-fno-stack-protector -fno-pic -mno-sse -mno-mmx -O2 -Wall -Wextra
LDFLAGS := -m elf_i386 -T linker.ld -nostdlib

.PHONY: all clean run

all: $(IMAGE)

$(OBJDIR):
	powershell -NoProfile -Command "New-Item -ItemType Directory -Force '$(OBJDIR)' | Out-Null"

$(OBJDIR)/boot.bin: src/boot/boot.asm | $(OBJDIR)
	$(NASM) -f bin $< -o $@

$(OBJDIR)/kernel.o: src/kernel/kernel.c | $(OBJDIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJDIR)/kernel.elf: $(OBJDIR)/kernel.o linker.ld
	$(LD) $(LDFLAGS) -o $@ $(OBJDIR)/kernel.o

$(OBJDIR)/kernel.bin: $(OBJDIR)/kernel.elf
	$(OBJCOPY) -O binary $< $@

$(IMAGE): $(OBJDIR)/boot.bin $(OBJDIR)/kernel.bin tools/mkimage.ps1
	powershell -NoProfile -ExecutionPolicy Bypass -File tools/mkimage.ps1 \
		-Boot $(OBJDIR)/boot.bin -Kernel $(OBJDIR)/kernel.bin \
		-Out $(IMAGE) -KernelSectors $(KERNEL_SECTORS)

run: $(IMAGE)
	qemu-system-i386 -drive format=raw,file=$(IMAGE)

clean:
	powershell -NoProfile -Command "Remove-Item -Recurse -Force '$(BUILD)' -ErrorAction SilentlyContinue"
