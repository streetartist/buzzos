BUILD := build
OBJDIR := $(BUILD)/obj/kernel
IMAGE := $(BUILD)/buzzos.img
KERNEL_SECTORS := 767
FS_START_SECTOR := 768
FS_SECTORS := 512
FS_IMAGE ?= $(IMAGE)
FS_TEST_IMAGE ?= $(BUILD)/buzzos-test.img
FS_REPAIR_IMAGE ?= $(BUILD)/buzzos-repaired.img

# Keep MSYS/Git shells from rewriting initrd virtual paths such as /bin/sh
# into Windows host paths when invoking native Python.
export MSYS2_ARG_CONV_EXCL := *
export MSYS_NO_PATHCONV := 1

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
	src/kernel/fs/procfs.c \
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
PYTHON ?= python
QEMU ?= qemu-system-i386

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
FUTEXHOLD_ELF := $(BUILD)/user/futexhold.elf
CAT_ELF := $(BUILD)/user/cat.elf
ECHO_ELF := $(BUILD)/user/echo.elf
GUI_APP_NAMES := guidemo notes forms calc
GUI_APP_ELFS := $(foreach app,$(GUI_APP_NAMES),$(BUILD)/user/$(app).elf)
GUI_APP_SRCS := $(foreach app,$(GUI_APP_NAMES),src/user/bin/$(app).c)
USER_ELFS := $(USER_ELF) $(SHELL_ELF) $(NANO_ELF) $(BASM_ELF) $(GUI_ELF) $(FUTEXHOLD_ELF) $(CAT_ELF) $(ECHO_ELF) $(GUI_APP_ELFS)
USER_SRCS := src/user/bin/hello.c src/user/bin/shell.c src/user/bin/nano.c src/user/bin/basm.c src/user/bin/gui.c src/user/bin/futexhold.c src/user/bin/cat.c src/user/bin/echo.c $(GUI_APP_SRCS)
USER_LIB  := src/user/libc/crt0.c src/user/libc/libc.c
INITRD_H := src/kernel/initrd.h
APP_REGISTRY_H := src/kernel/app_registry.h
GUI_APP_META := $(foreach app,$(GUI_APP_NAMES),$(wildcard src/user/bin/$(app).app src/user/bin/$(app).readme src/user/bin/$(app).seed))

.PHONY: all clean help doctor run run-current run-local run-gui run-guidemo run-notes run-forms run-calc check-project app-check app-registry fs-check fs-ls fs-repair fs-check-smoke fs-check-negative fs-check-repair smoke gui-smoke report verify image-reset-fs new-app

all: $(IMAGE)

help:
	$(PYTHON) tools/workflow.py

$(OBJDIR):
	powershell -NoProfile -Command "New-Item -ItemType Directory -Force '$(OBJDIR)' | Out-Null"

$(OBJDIR)/%.o: src/kernel/%.c | $(OBJDIR)
	powershell -NoProfile -Command "New-Item -ItemType Directory -Force (Split-Path '$@' -Parent) | Out-Null"
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJDIR)/core/kernel.o: $(INITRD_H) $(APP_REGISTRY_H)
$(OBJDIR)/core/exec.o: src/kernel/arch/i386/user.h
$(OBJDIR)/syscall/sys_proc.o: src/kernel/arch/i386/user.h
$(OBJDIR)/syscall/syscall.o: src/kernel/syscall/syscall_internal.h
$(OBJDIR)/syscall/sys_file.o: src/kernel/fs/minifs/minifs.h
$(OBJDIR)/sched/task.o: src/kernel/syscall/sys_ipc.h
$(OBJDIR)/syscall/sys_ipc.o: src/kernel/syscall/sys_ipc.h src/kernel/sched/task.h src/kernel/drv/timer.h
$(OBJDIR)/fs/minifs/minifs.o: src/kernel/fs/minifs/minifs.h
$(OBJDIR)/fs/procfs.o: src/kernel/mm/pmm.h src/kernel/sched/task.h src/kernel/net/net.h src/kernel/syscall/sys_ipc.h
$(OBJDIR)/core/elf.o: src/kernel/core/elf.h
$(OBJDIR)/arch/i386/paging.o: src/kernel/arch/i386/paging.h src/kernel/mm/pmm.h
$(OBJDIR)/mm/pmm.o: src/kernel/mm/pmm.h

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

$(BUILD)/user/%.o: src/user/bin/%.c src/user/libc/libc.h | $(BUILD)/user
	$(CC) $(UCFLAGS) -c $< -o $@

$(USER_ELF): $(BUILD)/user/crt0.o $(BUILD)/user/libc.o $(BUILD)/user/hello.o $(BUILD)/user/user.ld | $(BUILD)/user
	$(LD) -m elf_i386 -T $(BUILD)/user/user.ld -nostdlib -o $@ \
		$(BUILD)/user/crt0.o $(BUILD)/user/libc.o $(BUILD)/user/hello.o
	$(OBJCOPY) --strip-sections $@

$(SHELL_ELF): $(BUILD)/user/crt0.o $(BUILD)/user/libc.o $(BUILD)/user/shell.o $(BUILD)/user/user.ld | $(BUILD)/user
	$(LD) -m elf_i386 -T $(BUILD)/user/user.ld -nostdlib -o $@ \
		$(BUILD)/user/crt0.o $(BUILD)/user/libc.o $(BUILD)/user/shell.o
	$(OBJCOPY) --strip-sections $@

$(NANO_ELF): $(BUILD)/user/crt0.o $(BUILD)/user/libc.o $(BUILD)/user/nano.o $(BUILD)/user/user.ld | $(BUILD)/user
	$(LD) -m elf_i386 -T $(BUILD)/user/user.ld -nostdlib -o $@ \
		$(BUILD)/user/crt0.o $(BUILD)/user/libc.o $(BUILD)/user/nano.o
	$(OBJCOPY) --strip-sections $@

$(BASM_ELF): $(BUILD)/user/crt0.o $(BUILD)/user/libc.o $(BUILD)/user/basm.o $(BUILD)/user/user.ld | $(BUILD)/user
	$(LD) -m elf_i386 -T $(BUILD)/user/user.ld -nostdlib -o $@ \
		$(BUILD)/user/crt0.o $(BUILD)/user/libc.o $(BUILD)/user/basm.o
	$(OBJCOPY) --strip-sections $@

$(GUI_ELF): $(BUILD)/user/crt0.o $(BUILD)/user/libc.o $(BUILD)/user/gui.o $(BUILD)/user/user.ld | $(BUILD)/user
	$(LD) -m elf_i386 -T $(BUILD)/user/user.ld -nostdlib -o $@ \
		$(BUILD)/user/crt0.o $(BUILD)/user/libc.o $(BUILD)/user/gui.o
	$(OBJCOPY) --strip-sections $@

$(BUILD)/user/%.elf: $(BUILD)/user/crt0.o $(BUILD)/user/libc.o $(BUILD)/user/%.o $(BUILD)/user/user.ld | $(BUILD)/user
	$(LD) -m elf_i386 -T $(BUILD)/user/user.ld -nostdlib -o $@ \
		$(BUILD)/user/crt0.o $(BUILD)/user/libc.o $(BUILD)/user/$*.o
	$(OBJCOPY) --strip-sections $@

$(INITRD_H): $(USER_ELFS) tools/mkinitrd.py
	$(PYTHON) tools/mkinitrd.py /hello $(USER_ELF) /bin/sh $(SHELL_ELF) \
		/bin/nano $(NANO_ELF) /bin/basm $(BASM_ELF) /bin/gui $(GUI_ELF) \
		/bin/futexhold $(FUTEXHOLD_ELF) /bin/cat $(CAT_ELF) /bin/echo $(ECHO_ELF) \
		$(foreach app,$(GUI_APP_NAMES),/bin/$(app) $(BUILD)/user/$(app).elf) > $@

$(APP_REGISTRY_H): tools/gen_app_registry.py Makefile $(GUI_APP_META)
	$(PYTHON) tools/gen_app_registry.py --apps "$(GUI_APP_NAMES)" --out $@

.PHONY: user
user: $(INITRD_H)
	@echo "User program built and initrd updated. Run 'make' to rebuild kernel."

doctor:
	$(PYTHON) tools/doctor.py --python "$(PYTHON)" --make "$(MAKE)" --qemu "$(QEMU)"

run: $(IMAGE)
	$(QEMU) -drive format=raw,file=$(IMAGE) -serial stdio -no-reboot -netdev user,id=n0 -device ne2k_isa,netdev=n0,iobase=0x300,irq=10

run-current:
	powershell -NoProfile -Command "if (!(Test-Path '$(IMAGE)')) { throw '$(IMAGE) does not exist. Run make first.' }"
	$(QEMU) -drive format=raw,file=$(IMAGE) -serial stdio -no-reboot -netdev user,id=n0 -device ne2k_isa,netdev=n0,iobase=0x300,irq=10

run-local:
	powershell -NoProfile -ExecutionPolicy Bypass -File scripts/run-local.ps1 -Qemu "$(QEMU)"

run-gui:
	powershell -NoProfile -ExecutionPolicy Bypass -File scripts/run-local.ps1 -Qemu "$(QEMU)" -Command gui

run-guidemo:
	powershell -NoProfile -ExecutionPolicy Bypass -File scripts/run-local.ps1 -Qemu "$(QEMU)" -Command guidemo

run-notes:
	powershell -NoProfile -ExecutionPolicy Bypass -File scripts/run-local.ps1 -Qemu "$(QEMU)" -Command notes

run-forms:
	powershell -NoProfile -ExecutionPolicy Bypass -File scripts/run-local.ps1 -Qemu "$(QEMU)" -Command forms

run-calc:
	powershell -NoProfile -ExecutionPolicy Bypass -File scripts/run-local.ps1 -Qemu "$(QEMU)" -Command calc

check-project: $(IMAGE)
	$(PYTHON) tools/check_project.py

app-check: $(GUI_APP_ELFS) $(APP_REGISTRY_H)
	$(PYTHON) tools/check_project.py --apps-only

app-registry: $(APP_REGISTRY_H)
	@echo "Generated $(APP_REGISTRY_H)"

fs-check: $(IMAGE)
	$(PYTHON) tools/check_minifs.py --image "$(FS_IMAGE)"

fs-ls: $(IMAGE)
	$(PYTHON) tools/check_minifs.py --image "$(FS_IMAGE)" --list

fs-repair: $(IMAGE)
	$(PYTHON) tools/check_minifs.py --image "$(FS_IMAGE)" --repair --out "$(FS_REPAIR_IMAGE)"

smoke: $(IMAGE)
	powershell -NoProfile -ExecutionPolicy Bypass -File scripts/smoke.ps1 -Image $(IMAGE) -Qemu "$(QEMU)"

fs-check-smoke: smoke
	$(PYTHON) tools/check_minifs.py --image "$(FS_TEST_IMAGE)"

fs-check-negative: fs-check-smoke
	$(PYTHON) tools/check_minifs_negative.py --image "$(FS_TEST_IMAGE)"

fs-check-repair: fs-check-smoke
	$(PYTHON) tools/check_minifs_repair.py --image "$(FS_TEST_IMAGE)"

gui-smoke: $(IMAGE)
	powershell -NoProfile -ExecutionPolicy Bypass -File scripts/gui-smoke.ps1 -Image $(IMAGE) -Qemu "$(QEMU)" -PythonPath "$(PYTHON)"

report: $(IMAGE)
	$(PYTHON) tools/project_report.py --out "$(BUILD)/project-report.md" --print --python "$(PYTHON)" --make "$(MAKE)" --qemu "$(QEMU)"

verify: check-project smoke fs-check-smoke fs-check-negative fs-check-repair gui-smoke

image-reset-fs: $(OBJDIR)/boot.bin $(OBJDIR)/kernel.bin tools/mkimage.ps1
	powershell -NoProfile -ExecutionPolicy Bypass -File tools/mkimage.ps1 \
		-Boot $(OBJDIR)/boot.bin -Kernel $(OBJDIR)/kernel.bin \
		-Out $(IMAGE) -KernelSectors $(KERNEL_SECTORS) \
		-FsStart $(FS_START_SECTOR) -FsSectors $(FS_SECTORS) -ResetFs

new-app:
	$(PYTHON) tools/new_app.py $(APP)


clean:
	powershell -NoProfile -Command "Remove-Item -Recurse -Force '$(BUILD)' -ErrorAction SilentlyContinue"
