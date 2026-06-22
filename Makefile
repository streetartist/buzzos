BUILD := build
BUILD_ID := $(shell powershell -NoProfile -Command "[DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()")
OBJDIR := $(BUILD)/obj/$(BUILD_ID)
IMAGE := $(BUILD)/buzzos.img
KERNEL_SECTORS := 96

KERNEL_SRCS := \
	src/kernel/kernel.c \
	src/kernel/serial.c \
	src/kernel/vga.c \
	src/kernel/gdt.c \
	src/kernel/idt.c \
	src/kernel/pmm.c \
	src/kernel/paging.c \
	src/kernel/elf.c \
	src/kernel/user.c \
	src/kernel/task.c \
	src/kernel/syscall.c \
	src/kernel/vfs.c \
	src/kernel/keyboard.c \
	src/kernel/timer.c \
	src/kernel/shell.c \
	src/kernel/netdev.c \
	src/kernel/net.c

DRV_SRCS := src/drv/ne2000.c

KERNEL_ASMS := src/kernel/isr.asm src/kernel/switch.asm

KERNEL_C_OBJS  := $(patsubst src/kernel/%.c,$(OBJDIR)/%.o,$(KERNEL_SRCS))
DRV_OBJS       := $(patsubst src/drv/%.c,$(OBJDIR)/drv_%.o,$(DRV_SRCS))
KERNEL_ASM_OBJS:= $(patsubst src/kernel/%.asm,$(OBJDIR)/%.o-asm,$(KERNEL_ASMS))
KERNEL_OBJS    := $(KERNEL_C_OBJS) $(DRV_OBJS) $(KERNEL_ASM_OBJS)

NASM := nasm
CC   := clang
LD   := ld.lld
OBJCOPY := llvm-objcopy
QEMU := "D:/Program Files/qemu/qemu-system-i386.exe"

CFLAGS  := --target=i386-none-elf -std=c11 -ffreestanding -fno-builtin \
	-fno-stack-protector -fno-pic -mno-sse -mno-mmx -O2 -Wall -Wextra \
	-Isrc/kernel
LDFLAGS := -m elf_i386 -T linker.ld -nostdlib

# User program (GCC-compiled test binary)
USER_ELF := $(BUILD)/user/hello.elf
INITRD_H := src/kernel/initrd.h

.PHONY: all clean run

all: $(IMAGE)

$(OBJDIR):
	powershell -NoProfile -Command "New-Item -ItemType Directory -Force '$(OBJDIR)' | Out-Null"

$(OBJDIR)/%.o: src/kernel/%.c | $(OBJDIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJDIR)/drv_%.o: src/drv/%.c | $(OBJDIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJDIR)/%.o-asm: src/kernel/%.asm | $(OBJDIR)
	$(NASM) -f elf32 $< -o $@

$(OBJDIR)/boot.bin: src/boot/boot.asm | $(OBJDIR)
	$(NASM) -f bin $< -o $@

$(OBJDIR)/kernel.elf: $(KERNEL_OBJS) linker.ld | $(OBJDIR)
	$(LD) $(LDFLAGS) -o $@ $(KERNEL_OBJS)

$(OBJDIR)/kernel.bin: $(OBJDIR)/kernel.elf
	$(OBJCOPY) -O binary $< $@

$(IMAGE): $(OBJDIR)/boot.bin $(OBJDIR)/kernel.bin tools/mkimage.ps1
	powershell -NoProfile -ExecutionPolicy Bypass -File tools/mkimage.ps1 \
		-Boot $(OBJDIR)/boot.bin -Kernel $(OBJDIR)/kernel.bin \
		-Out $(IMAGE) -KernelSectors $(KERNEL_SECTORS)

# GCC-compiled user program → initrd
$(BUILD)/user:
	powershell -NoProfile -Command "New-Item -ItemType Directory -Force '$(BUILD)/user' | Out-Null"

build/user/user.ld: | $(BUILD)/user
	@echo 'ENTRY(_start)' > $@
	@echo 'SECTIONS { . = 0x200000; .text : { *(.text.entry) *(.text*) } .rodata : { *(.rodata*) } .data : { *(.data*) } .bss : { *(.bss*) *(COMMON) } }' >> $@

$(USER_ELF): src/user/hello.c build/user/user.ld | $(BUILD)/user
	$(CC) $(CFLAGS) -c $< -o $(BUILD)/user/hello.o
	$(LD) -m elf_i386 -T build/user/user.ld -nostdlib -o $@ $(BUILD)/user/hello.o

$(INITRD_H): $(USER_ELF) tools/mkinitrd.py
	python tools/mkinitrd.py $(USER_ELF) > $@
run: $(IMAGE)
	$(QEMU) -drive format=raw,file=$(IMAGE) -serial stdio -no-reboot -netdev user,id=n0 -device ne2k_isa,netdev=n0,iobase=0x300,irq=10


clean:
	powershell -NoProfile -Command "Remove-Item -Recurse -Force '$(BUILD)' -ErrorAction SilentlyContinue"
