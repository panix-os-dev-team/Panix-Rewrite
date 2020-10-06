/**
 * @file heap.cpp
 * @author Goswin von Brederlow (goswin-v-b@web.de)
 * @author Keeton Feavel (keetonfeavel@cedarville.edu)
 * @brief
 * @version 0.3
 * @date 2019-11-22
 *
 * @copyright Copyright the Panix Contributors (c) 2019
 * Code shamelessly taken from the OSDev Wiki. The article
 * can be found at the link below.
 * 
 * https://wiki.osdev.org/User:Mrvn/LinkedListBucketHeapImplementation
 * 
 */

#include <stddef.h>
#include <lib/stdio.hpp>
#include <lib/assert.hpp>
#include <lib/string.hpp>
#include <mem/heap.hpp>
#include <mem/paging.hpp>
#include <dev/serial/rs232.hpp>
#include <sys/panic.hpp>

#define NUM_SLOTS 1024
#define CONTAINER(C, l, v) ((C*)(((char*)v) - (intptr_t)&(((C*)0)->l)))

size_t mem_free = 0;
size_t mem_used = 0;
size_t mem_meta = 0;

px_heap_chunk_t* free_chunk[NUM_SIZES] = { NULL };
px_heap_chunk_t* first = NULL;
px_heap_chunk_t* last = NULL;

void* slot[NUM_SLOTS] = { NULL };
#if defined(DEBUG)
char dbg_buf[64] = {0};
#endif

static void px_memory_chunk_init(px_heap_chunk_t* chunk) {
#if defined(DEBUG)
    px_ksprintf(dbg_buf, "%s(0x%p)\n", __FUNCTION__, chunk);
    px_rs232_print(dbg_buf);
#endif    
    dlist_init(&chunk->all);
    chunk->used = 0;
    dlist_init(&chunk->free);
}

static size_t px_memory_chunk_size(const px_heap_chunk_t* chunk) {
#if defined(DEBUG)
    px_ksprintf(dbg_buf, "%s(0x%p)\n", __FUNCTION__, chunk);
    px_rs232_print(dbg_buf);
#endif    
    char* end = (char*)(chunk->all.next);
    char* start = (char*)(&chunk->all);
    return (end - start) - HEADER_SIZE;
}

static int px_memory_chunk_slot(size_t size) {
    int n = -1;
    while(size > 0) {
	    ++n;
	    size /= 2;
    }
    return n;
}

static void px_remove_free(px_heap_chunk_t* chunk) {
    size_t len = px_memory_chunk_size(chunk);
    int n = px_memory_chunk_slot(len);
#if defined(DEBUG)
    px_ksprintf(dbg_buf, "%s(0x%p) : removing chunk 0x%lx [%d]\n", __FUNCTION__, chunk, len, n);
    px_rs232_print(dbg_buf);
#endif
    DLIST_REMOVE_FROM(&free_chunk[n], chunk, free);
    mem_free -= len - HEADER_SIZE;
}

static void px_push_free(px_heap_chunk_t* chunk) {
    size_t len = px_memory_chunk_size(chunk);
    int n = px_memory_chunk_slot(len);
#if defined(DEBUG)
    px_ksprintf(dbg_buf, "%s(0x%p) : adding chunk 0x%lx [%d]\n", __FUNCTION__, chunk, len, n);
    px_rs232_print(dbg_buf);
#endif
    DLIST_PUSH(&free_chunk[n], chunk, free);
    mem_free += len - HEADER_SIZE;
}

void check(void) {
	int	i;
    px_heap_chunk_t* t = last;

	DLIST_ITERATOR_BEGIN(first, all, it) {
		assert(CONTAINER(px_heap_chunk_t, all, it->all.prev) == t);
		t = it;
    } DLIST_ITERATOR_END(it);

    for(i = 0; i < NUM_SIZES; ++i) {
		if (free_chunk[i]) {
			t = CONTAINER(px_heap_chunk_t, free, free_chunk[i]->free.prev);
			DLIST_ITERATOR_BEGIN(free_chunk[i], free, it) {
			assert(CONTAINER(px_heap_chunk_t, free, it->free.prev) == t);
			t = it;
			} DLIST_ITERATOR_END(it);
		}
    }
}

void px_heap_init(size_t size) {
    // Request a new page with the given size. Get the page address.
    void* mem = px_get_new_page(size);

    // Use the page we got as the memory for the heap
    char* mem_start = (char*)(((uintptr_t)mem + ALIGN - 1) & (~(ALIGN - 1)));
    char* mem_end = (char*)(((uintptr_t)mem + size) & (~(ALIGN - 1)));
    first = (px_heap_chunk_t*)mem_start;
    px_heap_chunk_t* second = first + 1;
    last = ((px_heap_chunk_t*)mem_end) - 1;

    // Initialize the heap chunks
    px_memory_chunk_init(first);
    px_memory_chunk_init(second);
    px_memory_chunk_init(last);
    dlist_insert_after(&first->all, &second->all);
    dlist_insert_after(&second->all, &last->all);

    // make first/last as used so they never get merged
    first->used = 1;
    last->used = 1;
    // Get the sizes and number of chunks
    size_t len = px_memory_chunk_size(second);
    int n = px_memory_chunk_slot(len);
#if defined(DEBUG)
    px_ksprintf(dbg_buf, "%s(0x%p, 0x%lx) : adding chunk 0x%lx [%d]\n", __FUNCTION__, mem, size, len, n);
    px_rs232_print(dbg_buf);
#endif
    // Push them onto the linked list and update tracking variables
    DLIST_PUSH(&free_chunk[n], second, free);
    mem_free = len - HEADER_SIZE;
    mem_meta = sizeof(px_heap_chunk_t) * 2 + HEADER_SIZE;
}

void* malloc(size_t size) {
#if defined(DEBUG)
    px_ksprintf(dbg_buf, "%s(0x%lx)\n", __FUNCTION__, size);
    px_rs232_print(dbg_buf);
#endif
    size = (size + ALIGN - 1) & (~(ALIGN - 1));

	if (size < MIN_SIZE) {
        size = MIN_SIZE;
    }

	int n = px_memory_chunk_slot(size - 1) + 1;

	if (n >= NUM_SIZES) {
        return NULL;
    }

	while(!free_chunk[n]) {
		++n;
		if (n >= NUM_SIZES) return NULL;
    }

	px_heap_chunk_t* chunk = DLIST_POP(&free_chunk[n], free);
    size_t size2 = px_memory_chunk_size(chunk);
#if defined(DEBUG)
	px_ksprintf(dbg_buf, "@ 0x%p [0x%lx]\n", chunk, size2);
    px_rs232_print(dbg_buf);
#endif
    size_t len = 0;

	if (size + sizeof(px_heap_chunk_t) <= size2) {
		px_heap_chunk_t* chunk2 = (px_heap_chunk_t*)(((char*)chunk) + HEADER_SIZE + size);
		px_memory_chunk_init(chunk2);
		dlist_insert_after(&chunk->all, &chunk2->all);
		len = px_memory_chunk_size(chunk2);
		int n = px_memory_chunk_slot(len);
#if defined(DEBUG)
		px_ksprintf(dbg_buf, "  adding chunk @ 0x%p 0x%lx [%d]\n", chunk2, len, n);
        px_rs232_print(dbg_buf);
#endif
		DLIST_PUSH(&free_chunk[n], chunk2, free);
		mem_meta += HEADER_SIZE;
		mem_free += len - HEADER_SIZE;
    }

	chunk->used = 1;
    memset(chunk->data, 0xAA, size);
#if defined(DEBUG)    
	px_ksprintf(dbg_buf, "AAAA\n");
    px_rs232_print(dbg_buf);
#endif
    mem_free -= size2;
    mem_used += size2 - len - HEADER_SIZE;
#if defined(DEBUG)
    px_ksprintf(dbg_buf, "  = %p [%p]\n", chunk->data, chunk);
    px_rs232_print(dbg_buf);
#endif
    return chunk->data;
}

void free(void* mem) {
    px_heap_chunk_t* chunk = (px_heap_chunk_t*)((char*)mem - HEADER_SIZE);
    px_heap_chunk_t* next = CONTAINER(px_heap_chunk_t, all, chunk->all.next);
    px_heap_chunk_t* prev = CONTAINER(px_heap_chunk_t, all, chunk->all.prev);
#if defined(DEBUG)
	px_ksprintf(dbg_buf, "%s(0x%p): 0x@%p 0x%lx [%d]\n", 
        __FUNCTION__,
        mem,
        chunk,
        px_memory_chunk_size(chunk),
        px_memory_chunk_slot(px_memory_chunk_size(chunk))
    );
    px_rs232_print(dbg_buf);
#endif
    mem_used -= px_memory_chunk_size(chunk);

    if (next->used == 0) {
		// merge in next
		px_remove_free(next);
		dlist_remove(&next->all);
		memset(next, 0xDD, sizeof(px_heap_chunk_t));
		mem_meta -= HEADER_SIZE;
		mem_free += HEADER_SIZE;
    }
    if (prev->used == 0) {
		// merge to prev
		px_remove_free(prev);
		dlist_remove(&chunk->all);
		memset(chunk, 0xDD, sizeof(px_heap_chunk_t));
		px_push_free(prev);
		mem_meta -= HEADER_SIZE;
		mem_free += HEADER_SIZE;
    } else {
		// make chunk as free
		chunk->used = 0;
		dlist_init(&chunk->free);
		px_push_free(chunk);
    }
}
