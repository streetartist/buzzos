#include "paging.h"
#include "pmm.h"
#include "serial.h"

__attribute__((aligned(4096))) static uint32_t page_directory[1024];
__attribute__((aligned(4096))) static uint32_t page_table_0[1024];
__attribute__((aligned(4096))) static uint32_t page_table_1[1024];

enum {
    USER_SPACE_START = 0x001C0000,
    USER_SPACE_END   = 0x00230000,
};

static void zero_page(uint32_t *page) {
    for (int i = 0; i < 1024; i++)
        page[i] = 0;
}

static void flush_tlb(void) {
    __asm__ volatile("mov %%cr3, %%eax\nmov %%eax, %%cr3" ::: "eax", "memory");
}

void paging_init(void) {
    serial_puts("[page] setting up paging...\n");

    for (int i = 0; i < 1024; i++) {
        page_directory[i] = 0;
        page_table_0[i]   = 0;
        page_table_1[i]   = 0;
    }

    /* Identity map 8 MiB for kernel access. User processes get private
     * mappings for their low user window in their own page directory. */
    for (int i = 0; i < 1024; i++) {
        page_table_0[i] = (i * 4096) | PAGE_PRESENT | PAGE_RW;
        page_table_1[i] = (0x400000 + i * 4096) | PAGE_PRESENT | PAGE_RW;
    }

    page_directory[0]   = ((uint32_t)(uintptr_t)page_table_0) | PAGE_PRESENT | PAGE_RW;
    page_directory[1]   = ((uint32_t)(uintptr_t)page_table_1) | PAGE_PRESENT | PAGE_RW;
    page_directory[768] = ((uint32_t)(uintptr_t)page_table_0) | PAGE_PRESENT | PAGE_RW;
    page_directory[769] = ((uint32_t)(uintptr_t)page_table_1) | PAGE_PRESENT | PAGE_RW;

    paging_switch((uint32_t)(uintptr_t)page_directory);
    uint32_t cr0;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= 0x80010000;
    __asm__ volatile("mov %0, %%cr0" : : "r"(cr0));
    __asm__ volatile("jmp 1f; 1:");

    serial_puts("[page] paging enabled (user accessible)\n");
}

uint32_t paging_current_cr3(void) {
    uint32_t cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    return cr3;
}

uint32_t paging_kernel_cr3(void) {
    return (uint32_t)(uintptr_t)page_directory;
}

void paging_switch(uint32_t cr3) {
    __asm__ volatile("mov %0, %%cr3" : : "r"(cr3) : "memory");
}

uint32_t paging_create_user_space(void) {
    uint32_t *pd = (uint32_t *)(uintptr_t)pmm_alloc_pages(1);
    uint32_t *low_pt = (uint32_t *)(uintptr_t)pmm_alloc_pages(1);
    if (!pd || !low_pt) {
        if (pd) pmm_free_pages((uintptr_t)pd, 1);
        if (low_pt) pmm_free_pages((uintptr_t)low_pt, 1);
        return 0;
    }

    zero_page(pd);
    zero_page(low_pt);

    for (int i = 0; i < 1024; i++)
        low_pt[i] = (i * 4096) | PAGE_PRESENT | PAGE_RW;

    pd[0] = ((uint32_t)(uintptr_t)low_pt) | PAGE_PRESENT | PAGE_RW | PAGE_USER;
    pd[1] = ((uint32_t)(uintptr_t)page_table_1) | PAGE_PRESENT | PAGE_RW;
    pd[768] = ((uint32_t)(uintptr_t)low_pt) | PAGE_PRESENT | PAGE_RW | PAGE_USER;
    pd[769] = ((uint32_t)(uintptr_t)page_table_1) | PAGE_PRESENT | PAGE_RW;

    for (uint32_t va = USER_SPACE_START; va < USER_SPACE_END; va += PAGE_SIZE) {
        uintptr_t phys = pmm_alloc_pages(1);
        if (!phys) {
            paging_destroy_user_space((uint32_t)(uintptr_t)pd);
            return 0;
        }
        uint32_t idx = va / PAGE_SIZE;
        low_pt[idx] = (uint32_t)phys | PAGE_PRESENT | PAGE_RW | PAGE_USER;
    }

    flush_tlb();
    return (uint32_t)(uintptr_t)pd;
}

void paging_destroy_user_space(uint32_t cr3) {
    if (!cr3 || cr3 == paging_kernel_cr3())
        return;

    uint32_t *pd = (uint32_t *)(uintptr_t)cr3;
    if (!(pd[0] & PAGE_PRESENT)) {
        pmm_free_pages((uintptr_t)pd, 1);
        return;
    }

    uint32_t *low_pt = (uint32_t *)(uintptr_t)(pd[0] & 0xFFFFF000u);
    for (uint32_t va = USER_SPACE_START; va < USER_SPACE_END; va += PAGE_SIZE) {
        uint32_t idx = va / PAGE_SIZE;
        if (low_pt[idx] & PAGE_PRESENT) {
            pmm_free_pages((uintptr_t)(low_pt[idx] & 0xFFFFF000u), 1);
            low_pt[idx] = 0;
        }
    }

    pmm_free_pages((uintptr_t)low_pt, 1);
    pmm_free_pages((uintptr_t)pd, 1);
}
