BUILD := build
BUILD_ID := $(shell powershell -NoProfile -Command "[DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()")
OBJDIR := $(BUILD)/obj/$(BUILD_ID)
IMAGE := $(BUILD)/buzzos.img
KERNEL_SECTORS := 384
FS_START_SECTOR := 512
FS_SECTORS := 512

KERNEL_SRCS := \
	src/kernel/core/kernel.c \
	src/kernel/core/elf.c \
	src/kernel/core/exec.c \
	src/kernel/arch/i386/gdt.c \
	src/kernel/arch/i386/idt.c \
	src/kernel/arch/i386/paging.c \
	src/kernel/arch/i386/user.c \
	src/kernel/mm/pmm.c \
	src/kernel/sched/task.c \
	src/kernel/syscall/syscall.c \
	src/kernel/syscall/sys_file.c \
	src/kernel/syscall/sys_proc.c \
	src/kernel/syscall/sys_net.c \
	src/kernel/syscall/sys_ipc.c \
	src/kernel/syscall/sys_gfx.c \
	src/kernel/fs/vfs.c \
	src/kernel/fs/ramfs.c \
	src/kernel/fs/devfs.c \
	src/kernel/fs/pipefs.c \
	src/kernel/fs/minifs_vfs.c \
	src/kernel/block/ata.c \
	src/kernel/block/cache.c \
	src/kernel/fs/minifs/minifs.c \
	src/kernel/net/netdev.c \
	src/kernel/net/net.c \
	src/kernel/drv/keyboard.c \
	src/kernel/drv/mouse.c \
	src/kernel/drv/timer.c \
	src/kernel/drv/serial.c \
	src/kernel/drv/vga.c \
	src/kernel/drv/reboot.c \
	src/kernel/drv/ne2000.c

KERNEL_ASMS := \
	src/kernel/arch/i386/isr.asm \
	src/kernel/arch/i386/switch.asm \
	src/kernel/arch/i386/setjmp.asm

KERNEL_C_OBJS  := $(patsubst src/kernel/%.c,$(OBJDIR)/%.o,$(KERNEL_SRCS))
KERNEL_ASM_OBJS:= $(patsubst src/kernel/%.asm,$(OBJDIR)/%.o-asm,$(KERNEL_ASMS))
KERNEL_OBJS    := $(KERNEL_C_OBJS) $(KERNEL_ASM_OBJS)

NASM := nasm
CC   := clang
LD   := ld.lld
OBJCOPY := llvm-objcopy
QEMU := "D:/Program Files/qemu/qemu-system-i386.exe"

KERNEL_INCLUDES := \
	-Isrc/kernel \
	-Isrc/kernel/core \
	-Isrc/kernel/arch/i386 \
	-Isrc/kernel/mm \
	-Isrc/kernel/sched \
	-Isrc/kernel/syscall \
	-Isrc/kernel/fs \
	-Isrc/kernel/fs/minifs \
	-Isrc/kernel/block \
	-Isrc/kernel/net \
	-Isrc/kernel/drv

CFLAGS  := --target=i386-none-elf -std=c11 -ffreestanding -fno-builtin \
	-fno-stack-protector -fno-pic -mno-sse -mno-mmx -O2 -Wall -Wextra \
	$(KERNEL_INCLUDES)

# User programs: allow x87 FPU (no SSE, but soft-float not needed)
UCFLAGS := --target=i386-none-elf -std=c11 -ffreestanding -fno-builtin \
	-fno-stack-protector -fno-pic -mno-sse -mno-mmx -mfpmath=387 -O2 \
	-Wall -Wextra -Isrc/user/libc
LDFLAGS := -m elf_i386 -T linker.ld -nostdlib

# User programs (linked with crt0 + libc)
USER_ELF := $(BUILD)/user/hello.elf
SHELL_ELF := $(BUILD)/user/shell.elf
NANO_ELF := $(BUILD)/user/nano.elf
BASM_ELF := $(BUILD)/user/basm.elf
GUI_ELF := $(BUILD)/user/gui.elf
USER_ELFS := $(USER_ELF) $(SHELL_ELF) $(NANO_ELF) $(BASM_ELF) $(GUI_ELF)
USER_SRCS := src/user/bin/hello.c src/user/bin/shell.c src/user/bin/nano.c src/user/bin/basm.c src/user/bin/gui.c
USER_LIB  := src/user/libc/crt0.c src/user/libc/libc.c
INITRD_H := src/kernel/initrd.h

.PHONY: all clean run run-current image-reset-fs

all: $(IMAGE)

$(OBJDIR):
	powershell -NoProfile -Command "New-Item -ItemType Directory -Force '$(OBJDIR)' | Out-Null"

$(OBJDIR)/%.o: src/kernel/%.c | $(OBJDIR)
	powershell -NoProfile -Command "New-Item -ItemType Directory -Force (Split-Path '$@' -Parent) | Out-Null"
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJDIR)/core/kernel.o: $(INITRD_H)

$(OBJDIR)/%.o-asm: src/kernel/%.asm | $(OBJDIR)
	powershell -NoProfile -Command "New-Item -ItemType Directory -Force (Split-Path '$@' -Parent) | Out-Null"
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

$(BUILD)/user/user.ld: | $(BUILD)/user
	@echo 'ENTRY(_start)' > $@
	@echo 'SECTIONS { . = 0x200000; .text : { *(.text.entry) *(.text*) } .rodata : { *(.rodata*) } .data : { *(.data*) } .bss : { *(.bss*) *(COMMON) } }' >> $@

$(BUILD)/user/crt0.o: src/user/libc/crt0.c src/user/libc/libc.h | $(BUILD)/user
	$(CC) $(UCFLAGS) -c src/user/libc/crt0.c -o $(BUILD)/user/crt0.o

$(BUILD)/user/libc.o: src/user/libc/libc.c src/user/libc/libc.h | $(BUILD)/user
	$(CC) $(UCFLAGS) -c src/user/libc/libc.c -o $(BUILD)/user/libc.o

$(BUILD)/user/hello.o: src/user/bin/hello.c src/user/libc/libc.h | $(BUILD)/user
	$(CC) $(UCFLAGS) -c src/user/bin/hello.c -o $(BUILD)/user/hello.o

$(BUILD)/user/shell.o: src/user/bin/shell.c src/user/libc/libc.h | $(BUILD)/user
	$(CC) $(UCFLAGS) -c src/user/bin/shell.c -o $(BUILD)/user/shell.o

$(BUILD)/user/nano.o: src/user/bin/nano.c src/user/libc/libc.h | $(BUILD)/user
	$(CC) $(UCFLAGS) -c src/user/bin/nano.c -o $(BUILD)/user/nano.o

$(BUILD)/user/basm.o: src/user/bin/basm.c src/user/libc/libc.h | $(BUILD)/user
	$(CC) $(UCFLAGS) -c src/user/bin/basm.c -o $(BUILD)/user/basm.o

$(BUILD)/user/gui.o: src/user/bin/gui.c src/user/libc/libc.h | $(BUILD)/user
	$(CC) $(UCFLAGS) -c src/user/bin/gui.c -o $(BUILD)/user/gui.o

$(USER_ELF): $(BUILD)/user/crt0.o $(BUILD)/user/libc.o $(BUILD)/user/hello.o $(BUILD)/user/user.ld | $(BUILD)/user
	$(LD) -m elf_i386 -T $(BUILD)/user/user.ld -nostdlib -o $@ \
		$(BUILD)/user/crt0.o $(BUILD)/user/libc.o $(BUILD)/user/hello.o

$(SHELL_ELF): $(BUILD)/user/crt0.o $(BUILD)/user/libc.o $(BUILD)/user/shell.o $(BUILD)/user/user.ld | $(BUILD)/user
	$(LD) -m elf_i386 -T $(BUILD)/user/user.ld -nostdlib -o $@ \
		$(BUILD)/user/crt0.o $(BUILD)/user/libc.o $(BUILD)/user/shell.o

$(NANO_ELF): $(BUILD)/user/crt0.o $(BUILD)/user/libc.o $(BUILD)/user/nano.o $(BUILD)/user/user.ld | $(BUILD)/user
	$(LD) -m elf_i386 -T $(BUILD)/user/user.ld -nostdlib -o $@ \
		$(BUILD)/user/crt0.o $(BUILD)/user/libc.o $(BUILD)/user/nano.o

$(BASM_ELF): $(BUILD)/user/crt0.o $(BUILD)/user/libc.o $(BUILD)/user/basm.o $(BUILD)/user/user.ld | $(BUILD)/user
	$(LD) -m elf_i386 -T $(BUILD)/user/user.ld -nostdlib -o $@ \
		$(BUILD)/user/crt0.o $(BUILD)/user/libc.o $(BUILD)/user/basm.o

$(GUI_ELF): $(BUILD)/user/crt0.o $(BUILD)/user/libc.o $(BUILD)/user/gui.o $(BUILD)/user/user.ld | $(BUILD)/user
	$(LD) -m elf_i386 -T $(BUILD)/user/user.ld -nostdlib -o $@ \
		$(BUILD)/user/crt0.o $(BUILD)/user/libc.o $(BUILD)/user/gui.o

$(INITRD_H): $(USER_ELFS) tools/mkinitrd.py
	python tools/mkinitrd.py /hello $(USER_ELF) /bin/sh $(SHELL_ELF) \
		/bin/nano $(NANO_ELF) /bin/basm $(BASM_ELF) /bin/gui $(GUI_ELF) > $@

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
