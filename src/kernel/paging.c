#include "paging.h"
#include "serial.h"

__attribute__((aligned(4096))) static uint32_t page_directory[1024];
__attribute__((aligned(4096))) static uint32_t page_table_0[1024];
__attribute__((aligned(4096))) static uint32_t page_table_1[1024];

void paging_init(void) {
    serial_puts("[page] setting up paging...\n");

    for (int i = 0; i < 1024; i++) {
        page_directory[i] = 0;
        page_table_0[i]   = 0;
        page_table_1[i]   = 0;
    }

    /* Identity map 8 MiB with USER flag for ring 3 access */
    for (int i = 0; i < 1024; i++) {
        page_table_0[i] = (i * 4096) | PAGE_PRESENT | PAGE_RW | PAGE_USER;
        page_table_1[i] = (0x400000 + i * 4096) | PAGE_PRESENT | PAGE_RW | PAGE_USER;
    }

    page_directory[0]   = ((uint32_t)(uintptr_t)page_table_0) | PAGE_PRESENT | PAGE_RW | PAGE_USER;
    page_directory[1]   = ((uint32_t)(uintptr_t)page_table_1) | PAGE_PRESENT | PAGE_RW | PAGE_USER;
    page_directory[768] = ((uint32_t)(uintptr_t)page_table_0) | PAGE_PRESENT | PAGE_RW | PAGE_USER;
    page_directory[769] = ((uint32_t)(uintptr_t)page_table_1) | PAGE_PRESENT | PAGE_RW | PAGE_USER;

    __asm__ volatile("mov %0, %%cr3" : : "r"((uint32_t)(uintptr_t)page_directory));
    uint32_t cr0;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= 0x80010000;
    __asm__ volatile("mov %0, %%cr0" : : "r"(cr0));
    __asm__ volatile("jmp 1f; 1:");

    serial_puts("[page] paging enabled (user accessible)\n");
}
