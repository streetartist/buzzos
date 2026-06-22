BUILD := build
BUILD_ID := $(shell powershell -NoProfile -Command "[DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()")
OBJDIR := $(BUILD)/obj/$(BUILD_ID)
IMAGE := $(BUILD)/buzzos.img
KERNEL_SECTORS := 256
FS_START_SECTOR := 512
FS_SECTORS := 256

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
	src/kernel/block/ata.c \
	src/kernel/block/cache.c \
	src/kernel/fs/minifs.c \
	src/kernel/keyboard.c \
	src/kernel/timer.c \
	src/kernel/reboot.c \
	src/kernel/exec.c \
	src/kernel/netdev.c \
	src/kernel/net.c

DRV_SRCS := src/drv/ne2000.c

KERNEL_ASMS := src/kernel/isr.asm src/kernel/switch.asm src/kernel/setjmp.asm

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

# User programs: allow x87 FPU (no SSE, but soft-float not needed)
UCFLAGS := --target=i386-none-elf -std=c11 -ffreestanding -fno-builtin \
	-fno-stack-protector -fno-pic -mno-sse -mno-mmx -mfpmath=387 -O2 \
	-Wall -Wextra -Isrc/user
LDFLAGS := -m elf_i386 -T linker.ld -nostdlib

# User programs (linked with crt0 + libc)
USER_ELF := $(BUILD)/user/hello.elf
SHELL_ELF := $(BUILD)/user/shell.elf
USER_ELFS := $(USER_ELF) $(SHELL_ELF)
USER_SRCS := src/user/hello.c src/user/shell.c
USER_LIB  := src/user/crt0.c src/user/libc.c
INITRD_H := src/kernel/initrd.h

.PHONY: all clean run run-current image-reset-fs

all: $(IMAGE)

$(OBJDIR):
	powershell -NoProfile -Command "New-Item -ItemType Directory -Force '$(OBJDIR)' | Out-Null"

$(OBJDIR)/%.o: src/kernel/%.c | $(OBJDIR)
	powershell -NoProfile -Command "New-Item -ItemType Directory -Force (Split-Path '$@' -Parent) | Out-Null"
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJDIR)/kernel.o: $(INITRD_H)

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
		-Out $(IMAGE) -KernelSectors $(KERNEL_SECTORS) \
		-FsStart $(FS_START_SECTOR) -FsSectors $(FS_SECTORS)

# GCC-compiled user program → initrd
$(BUILD)/user:
	powershell -NoProfile -Command "New-Item -ItemType Directory -Force '$(BUILD)/user' | Out-Null"

build/user/user.ld: | $(BUILD)/user
	@echo 'ENTRY(_start)' > $@
	@echo 'SECTIONS { . = 0x200000; .text : { *(.text.entry) *(.text*) } .rodata : { *(.rodata*) } .data : { *(.data*) } .bss : { *(.bss*) *(COMMON) } }' >> $@

$(BUILD)/user/crt0.o: src/user/crt0.c | $(BUILD)/user
	$(CC) $(UCFLAGS) -c src/user/crt0.c -o $(BUILD)/user/crt0.o

$(BUILD)/user/libc.o: src/user/libc.c src/user/libc.h | $(BUILD)/user
	$(CC) $(UCFLAGS) -c src/user/libc.c -o $(BUILD)/user/libc.o

$(BUILD)/user/hello.o: src/user/hello.c src/user/libc.h | $(BUILD)/user
	$(CC) $(UCFLAGS) -c src/user/hello.c -o $(BUILD)/user/hello.o

$(BUILD)/user/shell.o: src/user/shell.c src/user/libc.h | $(BUILD)/user
	$(CC) $(UCFLAGS) -c src/user/shell.c -o $(BUILD)/user/shell.o

$(USER_ELF): $(BUILD)/user/crt0.o $(BUILD)/user/libc.o $(BUILD)/user/hello.o build/user/user.ld | $(BUILD)/user
	$(LD) -m elf_i386 -T build/user/user.ld -nostdlib -o $@ \
		$(BUILD)/user/crt0.o $(BUILD)/user/libc.o $(BUILD)/user/hello.o

$(SHELL_ELF): $(BUILD)/user/crt0.o $(BUILD)/user/libc.o $(BUILD)/user/shell.o build/user/user.ld | $(BUILD)/user
	$(LD) -m elf_i386 -T build/user/user.ld -nostdlib -o $@ \
		$(BUILD)/user/crt0.o $(BUILD)/user/libc.o $(BUILD)/user/shell.o

$(INITRD_H): $(USER_ELFS) tools/mkinitrd.py
	python tools/mkinitrd.py /hello $(USER_ELF) /bin/sh $(SHELL_ELF) > $@

.PHONY: user
user: $(INITRD_H)
	@echo "User program built and initrd updated. Run 'make' to rebuild kernel."

run: $(IMAGE)
	$(QEMU) -drive format=raw,file=$(IMAGE) -serial stdio -no-reboot -netdev user,id=n0 -device ne2k_isa,netdev=n0,iobase=0x300,irq=10

run-current:
	powershell -NoProfile -Command "if (!(Test-Path '$(IMAGE)')) { throw '$(IMAGE) does not exist. Run make first.' }"
	$(QEMU) -drive format=raw,file=$(IMAGE) -serial stdio -no-reboot -netdev user,id=n0 -device ne2k_isa,netdev=n0,iobase=0x300,irq=10

image-reset-fs: $(OBJDIR)/boot.bin $(OBJDIR)/kernel.bin tools/mkimage.ps1
	powershell -NoProfile -ExecutionPolicy Bypass -File tools/mkimage.ps1 \
		-Boot $(OBJDIR)/boot.bin -Kernel $(OBJDIR)/kernel.bin \
		-Out $(IMAGE) -KernelSectors $(KERNEL_SECTORS) \
		-FsStart $(FS_START_SECTOR) -FsSectors $(FS_SECTORS) -ResetFs


clean:
	powershell -NoProfile -Command "Remove-Item -Recurse -Force '$(BUILD)' -ErrorAction SilentlyContinue"
