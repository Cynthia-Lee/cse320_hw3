/**
 * All functions you make for the assignment must be implemented in this file.
 * Do not submit your assignment with a main function in this file.
 * If you submit with a main function in this file, you will get a zero.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "debug.h"
#include "sfmm.h"
#include <errno.h>

size_t get_size(sf_block *bp) {
    return bp->header & BLOCK_SIZE_MASK;
}

int get_prev_alloc(sf_block *bp) {
    return bp->header & PREV_BLOCK_ALLOCATED;
}

int get_alloc(sf_block *bp) {
    return bp->header & THIS_BLOCK_ALLOCATED;
}

void *ftrp(sf_block *bp) {
    return (sf_block *)((void *)(bp) + get_size(bp) - sizeof(sf_footer));
}

void *next_blockp(sf_block *bp) {
    return (sf_block *)((void *)(bp) + get_size(bp));
}

void *prev_blockp(sf_block *bp) {
    return (sf_block *)((void *)(bp) - ((bp->prev_footer) & BLOCK_SIZE_MASK));
}

int sf_init(void) {
    // Create initial empty heap
    sf_mem_init();
    // alignment padding

    // prologue header
    sf_block *prologue = (sf_block *)((void *)sf_mem_start() + (sizeof(sf_header) * 6)); // 48
    prologue->header = (64 & BLOCK_SIZE_MASK) | PREV_BLOCK_ALLOCATED | THIS_BLOCK_ALLOCATED;

    // prologue footer
    sf_block *p_footer = (sf_block *)((void *)sf_mem_start() + (sizeof(sf_header) * 6) + (sizeof(sf_header) * 7));
    p_footer->header = (64 & BLOCK_SIZE_MASK) | PREV_BLOCK_ALLOCATED | THIS_BLOCK_ALLOCATED;

    if (sf_mem_grow() == NULL) { // extend the heap
        return -1;
    }

    // epilogue header
    sf_block *epilogue = (sf_block *)((void *)sf_mem_end() - (sizeof(sf_header) + sizeof(sf_footer)));
    epilogue->header = (0 & BLOCK_SIZE_MASK) | THIS_BLOCK_ALLOCATED;

    // the remainder of this memory should be inserted into the free list as a single block
    // this will be a "wilderness" block
    sf_block *wilderness = (sf_block *)((void *)sf_mem_start() + (sizeof(sf_header) * 6) + (sizeof(sf_header) * 8));
    wilderness->prev_footer = p_footer->header;
    wilderness->header = (3968 & BLOCK_SIZE_MASK) | PREV_BLOCK_ALLOCATED; // block not alloc
    // wilderness footer
    sf_block *w_footer = ftrp(wilderness);
    w_footer->header = (3968 & BLOCK_SIZE_MASK) | PREV_BLOCK_ALLOCATED;

    // epilogue->prev_footer = wilderness footer
    epilogue->prev_footer = w_footer->header;

    // initialize sf_free_list_heads
    int i;
    for (i = 0; i < NUM_FREE_LISTS; i++) {
        // dummy "sentinel" node, does not contain any data
        sf_free_list_heads[i].body.links.next = &sf_free_list_heads[i];
        sf_free_list_heads[i].body.links.prev = &sf_free_list_heads[i];
    }

    // struct sf_block sf_free_list_heads[NUM_FREE_LISTS];
    sf_free_list_heads[NUM_FREE_LISTS-1].body.links.next = wilderness;
    sf_free_list_heads[NUM_FREE_LISTS-1].body.links.prev = wilderness;
    wilderness->body.links.next = &sf_free_list_heads[NUM_FREE_LISTS-1];
    wilderness->body.links.prev = &sf_free_list_heads[NUM_FREE_LISTS-1];

    return 0;
}

int free_list_index(size_t size) {
    int start = 0;
    if (size <= 64) {
        start = 0;
    } else if (size <= (64*2)) {
        start = 1;
    } else if (size <= (64*3)) {
        start = 2;
    } else if (size <= (64*5)) {
        start = 3;
    } else if (size <= (64*8)) {
        start = 4;
    } else if (size <= (64*13)) {
        start = 5;
    } else if (size <= (64*21)) {
        start = 6;
    } else if (size <= (64*34)) {
        start = 7;
    } else { // size greater than 34M
        start = 8;
    }
    return start;
}

static void *find_fit(size_t size) {
    // First fit search
    sf_block *ptr = NULL;
    sf_block *head = NULL;
    int start = free_list_index(size);
    int i;
    // struct sf_block sf_free_list_heads[NUM_FREE_LISTS];
    for (i = start; i < NUM_FREE_LISTS; i++) {
        head = &sf_free_list_heads[i];
        ptr = head->body.links.next;
        while (ptr != head) {
            // found block
            if (size <= (((ptr->header) & BLOCK_SIZE_MASK) + sizeof(sf_footer))) {
                return ptr;
            }
            ptr = ptr->body.links.next;
        }
    }
    return NULL;
}

int is_wilderness(sf_block *p) {
    sf_block *epilogue = (sf_block *)((void *)sf_mem_end() - (sizeof(sf_header) + sizeof(sf_footer)));
    if (next_blockp(p) == epilogue) {
        return 1;
    } else {
        return 0;
    }
}

void remove_free_block(sf_block *p) {
    (p->body.links.prev)->body.links.next = p->body.links.next;
    (p->body.links.next)->body.links.prev = p->body.links.prev;
    p->body.links.prev = NULL;
    p->body.links.next = NULL;
}

void add_free_list(int index, sf_block *p) { // adds to the beginning of the freelist
    p->body.links.prev = &sf_free_list_heads[index];
    p->body.links.next = sf_free_list_heads[index].body.links.next;
    sf_free_list_heads[index].body.links.next = p;
    (p->body.links.next)->body.links.prev = p;
}

void new_epilogue() {
    sf_block *epilogue = (sf_block *)((void *)sf_mem_end() - (sizeof(sf_header) + sizeof(sf_footer)));
    epilogue->header = (0 & BLOCK_SIZE_MASK) | THIS_BLOCK_ALLOCATED;
    sf_block *wild = (sf_block *)sf_free_list_heads[NUM_FREE_LISTS-1].body.links.prev;
    if (wild != &sf_free_list_heads[NUM_FREE_LISTS-1]) {
        epilogue->prev_footer = wild->header;
    }
}

static void *coalesce(sf_block *p) {
    size_t prev_alloc = get_prev_alloc(p); // prev_alloc (this block)
    size_t size = get_size(p); // size (this block)
    sf_block *next_block = next_blockp(p);
    size_t next_alloc = get_alloc(next_block); // next_alloc

    sf_block *start = p;

    if (prev_alloc && next_alloc) { // Case 1
        // alloc, this, alloc
        //debug("case 1");
        return p;
    }

    else if (prev_alloc && !next_alloc) { // Case 2
        // alloc, this, free
        //debug("case 2");
        remove_free_block(p); // remove this free block
        remove_free_block(next_block); // remove old free block

        size += get_size(next_block); // size
        p->header = (size & BLOCK_SIZE_MASK) | PREV_BLOCK_ALLOCATED; // header
        sf_block *footer = ftrp(next_block);
        footer->header = p->header; // footer
    }

    else if (!prev_alloc && next_alloc) { // Case 3
        // free, this, alloc
        //debug("case 3");
        sf_block *prev_block = prev_blockp(p);
        if (is_wilderness(p)) { // case of right after expanded the heap
            remove_free_block(prev_block); // remove old free block
        } else {
            remove_free_block(prev_block); // remove old free block
            remove_free_block(p);
        }

        start = prev_block;
        size += get_size(prev_blockp(p)); // size
        if (get_prev_alloc(prev_block)) {
            prev_block->header = (size & BLOCK_SIZE_MASK) | PREV_BLOCK_ALLOCATED; // header
        } else {
            prev_block->header = (size & BLOCK_SIZE_MASK);
        }
        sf_block *footer = ftrp(p);
        footer->header = prev_block->header; // footer
    }

    else { // Case 4
        // free, this, free
        //debug("case 4");
        sf_block *prev_block = prev_blockp(p);
        remove_free_block(prev_block); // remove old free blocks
        remove_free_block(p);
        remove_free_block(next_block);

        start = prev_block;
        size += get_size(prev_blockp(p)) + get_size(next_block); // size
        if (get_prev_alloc(prev_block)) {
            prev_block->header = (size & BLOCK_SIZE_MASK) | PREV_BLOCK_ALLOCATED; // header
        } else {
            prev_block->header = (size & BLOCK_SIZE_MASK);
        }
        sf_block *footer = ftrp(next_block);
        footer->header = prev_block->header; // footer
    }

    int index = free_list_index(size);
    if (is_wilderness(start)) {
        index = NUM_FREE_LISTS - 1;
    }
    add_free_list(index, start); // prev, next

    return start;
}

// place block, split if needed
static void place(sf_block *ptr, size_t asize) {
    // debug("calling place");
    int check_wilderness = is_wilderness(ptr);
    // debug("%p", ptr);
    // check if can split without splinters
    // splinter = block less than the minimum block size
    size_t remainder_size = get_size(ptr) - asize;
    // can split
    if (remainder_size >= 64) {
        // splitting
        // "lower part" - allocation
        sf_block *lower = ptr;
        remove_free_block(lower);
        // "uper part" - remainder
        sf_block *upper = (sf_block *)((void *)ptr + asize);

        // lower: header, prev_footer
        if (get_prev_alloc(lower)) {
            lower->header = (asize & BLOCK_SIZE_MASK) | THIS_BLOCK_ALLOCATED | PREV_BLOCK_ALLOCATED;
        } else {
            lower->header = (asize & BLOCK_SIZE_MASK) | THIS_BLOCK_ALLOCATED;
        }
        // lower prev_alloc is same as before
        // lower prev_footer is same as before

        // upper: header, footer, prev, next, prev_footer
        upper->header = (remainder_size & BLOCK_SIZE_MASK) | PREV_BLOCK_ALLOCATED; // upper header alloc = 0
        // upper footer
        sf_block *upper_footer = (sf_block *)((void *)upper + remainder_size - sizeof(sf_footer));
        upper_footer->header = (remainder_size & BLOCK_SIZE_MASK) | PREV_BLOCK_ALLOCATED;
        // upper prev_footer
        upper->prev_footer = lower->header;

        // insert remainder back into the appropriate freelist
        // upper next, prev
        if (check_wilderness) {
            // put back wilderness block
            int index = NUM_FREE_LISTS - 1;
            sf_free_list_heads[index].body.links.prev = upper;
            sf_free_list_heads[index].body.links.next = upper;
            upper->body.links.prev = &sf_free_list_heads[index];
            upper->body.links.next = &sf_free_list_heads[index];
        } else {
            int index = free_list_index(remainder_size);
            // add remainder to freelist
            // LIFO
            add_free_list(index, upper); // next and prev set
        }
        coalesce(upper);

    } else {
        // no split
        // place data in free space
        if (get_prev_alloc(ptr)) {
            ptr->header = (asize & BLOCK_SIZE_MASK) | THIS_BLOCK_ALLOCATED | PREV_BLOCK_ALLOCATED;
        } else {
            ptr->header = (asize & BLOCK_SIZE_MASK) | THIS_BLOCK_ALLOCATED;
        }
        // next block prev_alloc
        sf_block *next = next_blockp(ptr);
        next->header = next->header | PREV_BLOCK_ALLOCATED;
        // remove from freelist
        remove_free_block(ptr);
     }
    // return ptr->body.payload;
}

static void split(sf_block *ptr, size_t asize) {
    // splitting a block with a pointer to an actual block
    // remainder does not have to be an actual block
    int check_wilderness = is_wilderness(ptr);
    // debug("%p", ptr);
    // check if can split without splinters
    // splinter = block less than the minimum block size
    size_t remainder_size = get_size(ptr) - asize;
    // can split
    if (remainder_size >= 64) {
        // splitting
        // "lower part" - allocation
        sf_block *lower = ptr;
        // remove_free_block(lower);
        // "uper part" - remainder
        sf_block *upper = (sf_block *)((void *)ptr + asize);

        // lower: header, prev_footer
        if (get_prev_alloc(lower)) {
            lower->header = (asize & BLOCK_SIZE_MASK) | THIS_BLOCK_ALLOCATED | PREV_BLOCK_ALLOCATED;
        } else {
            lower->header = (asize & BLOCK_SIZE_MASK) | THIS_BLOCK_ALLOCATED;
        }
        // lower prev_alloc is same as before
        // lower prev_footer is same as before

        // upper: header, footer, prev, next, prev_footer
        upper->header = (remainder_size & BLOCK_SIZE_MASK) | PREV_BLOCK_ALLOCATED; // upper header alloc = 0
        // upper footer
        sf_block *upper_footer = (sf_block *)((void *)upper + remainder_size - sizeof(sf_footer));
        upper_footer->header = (remainder_size & BLOCK_SIZE_MASK) | PREV_BLOCK_ALLOCATED;
        // upper prev_footer
        upper->prev_footer = lower->header;

        // insert remainder back into the appropriate freelist
        // upper next, prev
        if (check_wilderness) {
            // put back wilderness block
            int index = NUM_FREE_LISTS - 1;
            sf_free_list_heads[index].body.links.prev = upper;
            sf_free_list_heads[index].body.links.next = upper;
            upper->body.links.prev = &sf_free_list_heads[index];
            upper->body.links.next = &sf_free_list_heads[index];
        } else {
            int index = free_list_index(remainder_size);
            // add remainder to freelist
            // LIFO
            add_free_list(index, upper); // next and prev set
        }
        coalesce(upper);

    } else {
        // no split
        // place data in existing block, changing size
        if (get_prev_alloc(ptr)) {
            ptr->header = (asize & BLOCK_SIZE_MASK) | THIS_BLOCK_ALLOCATED | PREV_BLOCK_ALLOCATED;
        } else {
            ptr->header = (asize & BLOCK_SIZE_MASK) | THIS_BLOCK_ALLOCATED;
        }
     }
    // return ptr->body.payload;
}

int valid_pointer(void *pp) {
    // invalid pointers:
        // pointer is NULL
        // pointer is not aligned to a 64-byte boundary
        // allocated bit in the header is 0
        // header of the block is before the end of prologue,
            // or footer of the block is after the beginning of the epilogue
        // prev_alloc field is 0, indicated that the previous block is free,
            // but alloc field of the previous block header is not 0
    if (pp == NULL) {
        return 0;
    }

    sf_block *bp = (sf_block *)((void *)(pp) - (sizeof(sf_header) + sizeof(sf_footer)));

    // pointer is not alligned to a 64-byte boundary
    int not_alligned = (long int)pp % 64 != 0;

    // bp->header addr < prologue_end addr
    sf_block *prologue = (sf_block *)((void *)sf_mem_start() + (sizeof(sf_header) * 6)); // 48
    sf_block *prologue_end = ftrp(prologue) + sizeof(sf_footer);
    int before_end_prologue = (((void *)(bp) + sizeof(sf_header)) < ((void *)prologue_end));

    // bp->footer addr > epilogue_header addr
    int after_start_epilogue = (void *)ftrp(bp) > ((void *)sf_mem_end() - sizeof(sf_header));

    if (not_alligned || (get_alloc(bp) == 0)
        || before_end_prologue || after_start_epilogue
        || (get_prev_alloc(bp) == 0 && get_alloc(prev_blockp(bp)) != 0)) {

        return 0;
    }
    return 1;
}

void *sf_malloc(size_t size) { // size in bytes
    // initialize the heap if this is first call, heap empty
    if (sf_mem_start() == sf_mem_end()) {
        sf_init();
    }

    if (size == 0) {
        // without setting sf_errno
        return NULL;
    }
    // if the request size is non-zero, then should determine the size of block

    // aligned to 64-byte boundaries
    size_t asize = size + sizeof(sf_header); // Adjust block size

    if (asize > 64 && (asize % 64 != 0)) {
        // round up to the next multiple of alignment size
        asize = ((asize/64) + 1) * 64;
    } else if (asize < 64) {
        asize = 64;
    }

    // search free list
    sf_block *bp = find_fit(asize);
    if (bp != NULL) {
        place(bp, asize);
        return bp->body.payload;
    }

    // No fit found. Get more memory and place the block.
    int amount = asize;
    do {
        sf_block *ptr = sf_mem_grow(); // grow heap

        if (ptr == NULL) { // error, cannot grow any more, returns NULL and sets sf_errno to ENOMEM
            return NULL;
        }
        new_epilogue(); // new epilogue header
        // old epilogue becomes the header of the new block
        sf_block *page = (sf_block *)((void *)ptr - (sizeof(sf_header) + sizeof(sf_footer)));
        sf_block *prev_block = prev_blockp(page);

        if (get_alloc(prev_block)) { // prev_block is allocated
            page->header = (PAGE_SZ & BLOCK_SIZE_MASK) | PREV_BLOCK_ALLOCATED;
        } else {
            page->header = (PAGE_SZ & BLOCK_SIZE_MASK);
        }
        // after wilderness block
        sf_block *page_footer = ftrp(page);
        page_footer->header = page->header;
        // page (old epilogue) prev_footer is the same

        int no_wild = sf_free_list_heads[NUM_FREE_LISTS-1].body.links.next == &sf_free_list_heads[NUM_FREE_LISTS-1];
        if (no_wild) {
            add_free_list(NUM_FREE_LISTS-1, page);
        }

        // coalesce newly allocated page with any wilderness block immediately preceeding it
        // insert new wilderness block at the beginning of the last freelist
        bp = coalesce(page);

        amount -= PAGE_SZ;
    } while (amount > PAGE_SZ);

    // if cannot satisfy request, sf_malloc set sf_errno to ENOMEM and return NULL

    // place
    place(bp, asize);
    return bp->body.payload;

    // if no free block big enough, then use sf_mem_grow to request for more memory
    // requests larger than 1 page, more calls might be required
        // coalesce the newly allocated page with any wilderness block immidiately preceding it
        // insert new (wilderness) block at the begininng of the last freelist
}

void sf_free(void *pp) {
    // pointer address in int = (sf_block *)((void *)(pointer))

    // verify that the pointer being pass belongs to an allocated block
    // examining the fields of the block header and footer

    if (!valid_pointer(pp)) {
        abort();
        return;
    }
    // if invalid pointer is passed to function, must call "abort" to exit the program

    // after confirming that a valid pointer was given, you must free the block
    // first, the block must be coalesced with any adjacent free block
    // then, determine the class size appropriate for the (now-coalescede) block
    // insrt the block to the beginning of the free block for that size class
    // wilderness block must be inserted in the last free list

    // free block
    // prev_footer is the same
    sf_block *bp = (sf_block *)((void *)(pp) - (sizeof(sf_header) + sizeof(sf_footer)));
    bp->header = bp->header & ~(THIS_BLOCK_ALLOCATED); // make bit not allocated
    sf_block *footer = ftrp(bp); // footer
    footer->header = bp->header;

    // add newly freed block to free list
    int index = free_list_index(get_size(bp));
    add_free_list(index, bp); // next, prev
    // prev_footer is the same
    // change next block's previous alloc
    sf_block *next = next_blockp(bp);
    next->header = next->header & ~(PREV_BLOCK_ALLOCATED);

    coalesce(bp);

    // blocks in free list must not be marked as allocated, and have valid footer
    return;
}

void *sf_realloc(void *pp, size_t rsize) {
    // rsize is size of the payload
    // check if valid pointer
    if (!valid_pointer(pp)) {
        sf_errno = EINVAL; // set sf_errno = EINVAL
        return NULL;
    }
    // check if valid size (valid pointer)
    if (rsize == 0) {
        // free the block and return null
        sf_free(pp);
        return NULL;
    }

    sf_block *bp = (sf_block *)((void *)pp - (sizeof(sf_header) + sizeof(sf_footer)));
    size_t asize = rsize + sizeof(sf_header);
    if (asize > 64 && (asize % 64 != 0)) {
        // round up to the next multiple of alignment size
        asize = ((asize/64) + 1) * 64;
    } else if (asize < 64) {
        asize = 64;
    }

    if (get_size(bp) == (rsize + sizeof(sf_header))) {
        return bp->body.payload;
    }

    // reallocating to a larger size
    if (get_size(bp) < (rsize + sizeof(sf_header))) {
        // call sf_malloc to obtain a larger block
        void *dest = sf_malloc(rsize); // malloc returns pointer to region of mem
        // if no memory available, malloc set sf_errno = ENOMEM
        if (dest == NULL) {
            // if sf_malloc returns NULL, then sf_realloc must also return NULL
            return NULL;
        }
        // call memcpy to copy the data in the block given by the client to the block returned by sf_malloc
            // copy the entire payload area, but no more
        memcpy(dest, pp, rsize);
        sf_free(pp);
        return dest;
    } else { // reallocating to a smaller size


        // allocator must use the block that was passed by the caller, split the returned block
        // two cases for splitting:
        // if can split
        // else cannot split
            // if splinter, do not split, leave splinter in the block
            // updated the header
        split(bp, asize);
        return bp->body.payload;
    }

    return NULL;
}

void *sf_memalign(size_t size, size_t align) {
    // check that the requested alignment is at least as large as the minimum block size
    // check that the requested alignment is a power of two
    // if fail, sf_errno = EINVAL, return null

    if (align < 64 || !((align & (align - 1)) == 0)) { // not at least large as min, not power of 2
        sf_errno = EINVAL;
        return NULL;
    }

    // check passed

    // aligned to 64-byte boundaries
    size_t block_size = size + sizeof(sf_header); // Adjust block size

    if (block_size > 64 && (block_size % 64 != 0)) {
        // round up to the next multiple of alignment size
        block_size = ((block_size/64) + 1) * 64;
    } else if (block_size < 64) {
        block_size = 64;
    }

    //sf_block *prologue = (sf_block *)((void *)sf_mem_start() + (sizeof(sf_header) * 6)); // 48
    //sf_block *prologue_end = ftrp(prologue) + sizeof(sf_footer);
    //sf_block *start_payload_ptr = (void *)prologue_end + (sizeof(sf_header) *2);

    size_t asize = size + align + 64 + sizeof(sf_header);
    void *payload_ptr = sf_malloc(asize);
    sf_block *bp = payload_ptr - (sizeof(sf_header) * 2);
    size_t osize = get_size(bp);
    sf_block *new_bp = bp;

    if (((long int)payload_ptr) % align == 0) {
        //debug("OK");
        // if next space of the block can become a free block
        size_t next_size = get_size(bp) - block_size;
        if (next_size >= 64) {
            split(bp, block_size);
        }
        return bp->body.payload;
    } else {
        //debug("NOT OK");
        // move to aligned address for payload
        // find address to start the block

        // aligned to align boundary
        // round up to the next multiple of alignment size
        void *curr_payload_ptr = (void *)(((((long int)payload_ptr)/align) + 1) * align);
        new_bp = ((void *)curr_payload_ptr - (sizeof(sf_header) * 2));

        // if prev space of the block can become a free block
        size_t prev_size = (void *)new_bp - (void *)bp;
        if (prev_size >= 64) {
            if (get_prev_alloc(bp)) {
                bp->header = (prev_size & BLOCK_SIZE_MASK) | PREV_BLOCK_ALLOCATED;
            } else {
                bp->header = (prev_size & BLOCK_SIZE_MASK);
            }
            // prev_footer is the same
            sf_block *start_bp_footer = ftrp(bp);
            start_bp_footer->header = bp->header;
            int index = free_list_index(prev_size);
            add_free_list(index, bp);
            new_bp->header = ((osize - prev_size) & BLOCK_SIZE_MASK) | THIS_BLOCK_ALLOCATED;
        } else {
            if (get_prev_alloc(new_bp)) {
                new_bp->header = ((osize - prev_size) & BLOCK_SIZE_MASK) | PREV_BLOCK_ALLOCATED;
            } else {
                new_bp->header = ((osize - prev_size) & BLOCK_SIZE_MASK);
            }
        }
        // if next space of the block can become a free block
        size_t next_size = get_size(bp) - block_size - prev_size;
        if (next_size >= 64) {
            split(new_bp, block_size);
        }
    }

    // either the normal payload address of the block will satisfy the requested alignment
    // or there will be a large address within the block that satisfies the requested alignment
        // has sufficient space after it to hold the payload

    return new_bp->body.payload;
}

