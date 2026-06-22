#include "elf.h"

uint32_t elf_load(const uint8_t *buf) {
    const struct elf32_ehdr *ehdr = (const struct elf32_ehdr *)buf;

    /* Verify ELF magic */
    if (ehdr->e_ident[0] != ELF_MAG0 ||
        ehdr->e_ident[1] != ELF_MAG1 ||
        ehdr->e_ident[2] != ELF_MAG2 ||
        ehdr->e_ident[3] != ELF_MAG3)
        return 0;

    /* 32-bit, little-endian, executable */
    if (ehdr->e_ident[4] != ELF_CLASS32) return 0;
    if (ehdr->e_ident[5] != ELF_DATA2LSB) return 0;
    if (ehdr->e_type != ELF_ET_EXEC) return 0;
    if (ehdr->e_machine != ELF_EM_386) return 0;

    /* Load each PT_LOAD segment */
    for (uint16_t i = 0; i < ehdr->e_phnum; i++) {
        const struct elf32_phdr *phdr =
            (const struct elf32_phdr *)(buf + ehdr->e_phoff + i * ehdr->e_phentsize);

        if (phdr->p_type != PT_LOAD) continue;

        uint8_t *dest = (uint8_t *)(uintptr_t)phdr->p_vaddr;
        const uint8_t *src = buf + phdr->p_offset;

        /* Copy file data */
        for (uint32_t j = 0; j < phdr->p_filesz; j++) {
            dest[j] = src[j];
        }

        /* Zero-fill .bss portion */
        for (uint32_t j = phdr->p_filesz; j < phdr->p_memsz; j++) {
            dest[j] = 0;
        }
    }

    return ehdr->e_entry;
}
