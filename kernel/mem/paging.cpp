/**
 * @file paging.cpp
 * @author Keeton Feavel (keetonfeavel@cedarville.edu)
 * @author Micah Switzer (mswitzer@cedarville.edu)
 * @brief
 * @version 0.3
 * @date 2019-11-22
 *
 * @copyright Copyright Keeton Feavel and Micah Switzer (c) 2019
 *
 */
#include <arch/Arch.hpp>
#include <arch/Memory.hpp>
#include <boot/Arguments.hpp>
#include <lib/Bitset.hpp>
#include <lib/mutex.hpp>
#include <lib/stdio.hpp>
#include <lib/string.hpp>
#include <mem/Physical.hpp>
#include <mem/paging.hpp>
#include <meta/sections.hpp>
#include <stddef.h>

namespace Memory {

#define PAGE_COUNT(s)   ((s) / ARCH_PAGE_SIZE) + 1;

static Mutex pagingLock("paging");

static Physical::PhysicalManager physical;

// one bit for every page
static Bitset<MEM_BITMAP_SIZE> mappedPages;

static uintptr_t pageDirectoryAddress;
static struct Arch::Memory::Table* pageDirectoryVirtual[ARCH_PAGE_DIR_ENTRIES];

// both of these must be page aligned for anything to work right at all
static struct Arch::Memory::DirectoryEntry pageDirectoryPhysical[ARCH_PAGE_DIR_ENTRIES] SECTION(".page_tables,\"aw\", @nobits#");
static struct Arch::Memory::Table pageTables[ARCH_PAGE_TABLE_ENTRIES] SECTION(".page_tables,\"aw\", @nobits#");

// Function prototypes
static void pageFaultCallback(struct registers* regs);
static void initPhysical(MemoryMap* map);
static void initDirectory();
static void mapEarlyMem();
static void mapKernel();
static uintptr_t findNextFreeVirtualAddress(size_t seq);
static inline void mapKernelPageTable(size_t idx, struct Arch::Memory::Table* table);
static inline void setPageDirectory(uintptr_t Directory);
static void argumentsCallback(const char* arg);

// Kernel cmdline arguments
static bool is_mapping_output_enabled = false;
#define MAPPING_OUTPUT_FLAG "--enable-mapping-output"
KERNEL_PARAM(enableMappingLogs, MAPPING_OUTPUT_FLAG, argumentsCallback);

void init(MemoryMap* map)
{
    // we can set breakpoints or make a futile attempt to recover.
    register_interrupt_handler(ISR_PAGE_FAULT, pageFaultCallback);
    // populate the physical memory map based on bootloader information
    initPhysical(map);
    // init our structures
    initDirectory();
    // identity map the first 1 MiB of RAM
    mapEarlyMem();
    // map in our higher-half kernel
    mapKernel();
    // use our new set of page tables
    setPageDirectory(Arch::Memory::pageAlign(pageDirectoryAddress));
    // flush the tlb and we're off to the races!
    Arch::Memory::pagingEnable();
}

static void pageFaultCallback(struct registers* regs)
{
    PANIC(regs);
}

static void initPhysical(MemoryMap* map)
{
    size_t freeBytes = 0;
    size_t reservedBytes = 0;

    for (size_t i = 0; i < map->Count(); i++) {
        auto section = map->Get(i);
        if (section.initialized()) {
            switch (section.type())
            {
                case Available:
                    physical.setFree(section);
                    freeBytes += section.size();
                    break;
                default:
                    physical.setUsed(section);
                    reservedBytes += section.size();
                    break;
            }
        }
    }

    debugf("Available memory: %zu MB\n", B_TO_MB(freeBytes));
    debugf("Reserved memory: %zu MB\n", B_TO_MB(reservedBytes));
    debugf("Total memory: %zu MB\n", B_TO_MB(freeBytes + reservedBytes));
}

static inline void mapKernelPageTable(size_t idx, struct Arch::Memory::Table* table)
{
    pageDirectoryVirtual[idx] = table;
    pageDirectoryPhysical[idx] = {
        .present = 1,
        .readWrite = 1,
        .usermode = 0,
        .writeThrough = 0,
        .cacheDisable = 0,
        .accessed = 0,
        .ignoredA = 0,
        .size = 0,
        .ignoredB = 0,
        // compute the physical address of this page table the virtual address is obtained with the & operator and
        // the offset is applied from the load address of the kernel we must shift it over 12 bits because we only
        // care about the highest 20 bits for the page table
        .tableAddr = KADDR_TO_PHYS((uintptr_t)table) >> 12
    };
}

static void initDirectory()
{
    // For every page in kernel memory
    for (size_t i = 0; i < ARCH_PAGE_DIR_ENTRIES - 1; i++) {
        mapKernelPageTable(i, &pageTables[i]);
        // clear out the page tables
        for (size_t j = 0; j < ARCH_PAGE_DIR_ENTRIES; j++) {
            memset(&pageTables[i].pages[j], 0, sizeof(struct Arch::Memory::TableEntry));
        }
    }
    // recursively map the last page table to the page directory
    mapKernelPageTable(ARCH_PAGE_TABLE_ENTRIES - 1, (struct Arch::Memory::Table*)&pageDirectoryPhysical[0]);
    for (size_t i = ARCH_PAGE_TABLE_ENTRIES * (ARCH_PAGE_TABLE_ENTRIES - 1); i < ARCH_PAGE_TABLE_ENTRIES * ARCH_PAGE_TABLE_ENTRIES; i++) {
        mappedPages.Set(i);
    }
    // store the physical address of the page directory for quick access
    pageDirectoryAddress = KADDR_TO_PHYS((uintptr_t)&pageDirectoryPhysical[0]);
}

void mapKernelPage(union Arch::Memory::Address vaddr, union Arch::Memory::Address paddr)
{
    // Set the page directory entry (pde) and page table entry (pte)
    size_t pde = vaddr.page.dirIndex;
    size_t pte = vaddr.page.tableIndex;
    // If the page's virtual address is not aligned
    if (vaddr.page.offset != 0) {
        PANIC("Attempted to map a non-page-aligned virtual address.\n");
    }
    Arch::Memory::TableEntry* entry = &(pageTables[pde].pages[pte]);
    // Print a debug message to serial
    if (is_mapping_output_enabled) {
        debugf("map 0x%08lx to 0x%08lx, pde = 0x%08lx, pte = 0x%08lx\n", paddr.val, vaddr.val, pde, pte);
    }
    // If the page is already mapped into memory
    if (entry->present) {
        if (entry->frame == paddr.frame.index) {
            // this page was already mapped the same way
            return;
        }

        PANIC("Attempted to map already mapped page.\n");
    }
    // Set the page information
    pageTables[pde].pages[pte] = {
        .present = 1,               // The page is present
        .readWrite = 1,             // The page has r/w permissions
        .usermode = 0,              // These are kernel pages
        .writeThrough = 0,          // Disable write through
        .cacheDisable = 0,          // The page is cached
        .accessed = 0,              // The page is unaccessed
        .dirty = 0,                 // The page is clean
        .pageAttrTable = 0,         // The page has no attribute table
        .global = 0,                // The page is local
        .unused = 0,                // Ignored
        .frame = paddr.frame.index, // The last 20 bits are the frame
    };
    // Set the associated bit in the bitmaps
    physical.setUsed(paddr);
    mappedPages.Set(vaddr.frame.index);
}

void mapKernelRangeVirtual(Section sect)
{
    union Arch::Memory::Address a;
    for (a = ADDR(sect.base()); a.val < sect.end(); a.val += ARCH_PAGE_SIZE) {
        mapKernelPage(a, a);
    }
}

void mapKernelRangePhysical(Section sect)
{
    union Arch::Memory::Address a;
    for (a = ADDR(sect.base()); a.val < sect.end(); a.val += ARCH_PAGE_SIZE) {
        union Arch::Memory::Address phys {
            .val = KADDR_TO_PHYS(a.val)
        };
        mapKernelPage(a, phys);
    }
}

static void mapEarlyMem()
{
    debugf("==== MAP EARLY MEM ====\n");
    mapKernelRangeVirtual(Section(0x0, 0x100000));
}

static void mapKernel()
{
    debugf("==== MAP HH KERNEL ====\n");
    mapKernelRangePhysical(Section(KERNEL_START, KERNEL_END));
}

static inline void setPageDirectory(size_t page_dir)
{
    asm volatile("mov %0, %%cr3" ::"b"(page_dir));
}

/**
 * note: this can't find more than 32 sequential pages
 * @param seq the number of sequential pages to get
 */
static uintptr_t findNextFreeVirtualAddress(size_t seq)
{
    return mappedPages.FindFirstRange(seq, false);
}

void* newPage(size_t size)
{
    pagingLock.Lock();
    size_t page_count = PAGE_COUNT(size);
    size_t free_idx = findNextFreeVirtualAddress(page_count);
    if (free_idx == SIZE_MAX)
        return NULL;
    for (size_t i = free_idx; i < free_idx + page_count; i++) {
        size_t phys_page_idx = physical.findNextFreePhysicalAddress();
        if (phys_page_idx == SIZE_MAX)
            return NULL;
        union Arch::Memory::Address phys = ADDR(phys_page_idx * ARCH_PAGE_SIZE);
        mapKernelPage(ADDR(i * ARCH_PAGE_SIZE), phys);
    }
    pagingLock.Unlock();
    return (void*)(free_idx * ARCH_PAGE_SIZE);
}

void freePage(void* page, size_t size)
{
    pagingLock.Lock();
    size_t page_count = PAGE_COUNT(size);
    union Arch::Memory::Address addr = ADDR((uintptr_t)page);
    for (size_t i = addr.page.tableIndex; i < addr.page.tableIndex + page_count; i++) {
        mappedPages.Reset(i);
        // this is the same as the line above
        struct Arch::Memory::TableEntry* pte = &(pageTables[i / ARCH_PAGE_TABLE_ENTRIES].pages[i % ARCH_PAGE_TABLE_ENTRIES]);
        // the frame field is actually the page frame's index basically it's frame 0, 1...(2^21-1)
        physical.setFree(pte->frame);
        // zero it out to unmap it
        memset(pte, 0, sizeof(struct Arch::Memory::TableEntry));
        // clear that tlb
        Arch::Memory::pageInvalidate(page);
    }
    pagingLock.Unlock();
}

bool isPresent(uintptr_t addr)
{
    // Convert the address into an index and check whether the page is in the bitmap
    return mappedPages[addr >> 12];
}

// TODO: maybe enforce access control here in the future
uintptr_t getPageDirPhysAddr()
{
    return pageDirectoryAddress;
}

static void argumentsCallback(const char* arg)
{
    if (strcmp(arg, MAPPING_OUTPUT_FLAG) == 0) {
        debugf("is_mapping_output_enabled = true");
        is_mapping_output_enabled = true;
    }
}

} // !namespace Paging
