#include <criterion/criterion.h>
#include <errno.h>
#include <signal.h>
#include "debug.h"
#include "sfmm.h"

void assert_free_block_count(size_t size, int count);
void assert_free_list_block_count(size_t size, int count);

/*
 * Assert the total number of free blocks of a specified size.
 * If size == 0, then assert the total number of all free blocks.
 */
void assert_free_block_count(size_t size, int count) {
    int cnt = 0;
    for(int i = 0; i < NUM_FREE_LISTS; i++) {
	sf_block *bp = sf_free_list_heads[i].body.links.next;
	while(bp != &sf_free_list_heads[i]) {
	    if(size == 0 || size == (bp->header & BLOCK_SIZE_MASK))
		cnt++;
	    bp = bp->body.links.next;
	}
    }
    if(size == 0) {
	cr_assert_eq(cnt, count, "Wrong number of free blocks (exp=%d, found=%d)",
		     count, cnt);
    } else {
	cr_assert_eq(cnt, count, "Wrong number of free blocks of size %ld (exp=%d, found=%d)",
		     size, count, cnt);
    }
}

/*
 * Assert that the free list with a specified index has the specified number of
 * blocks in it.
 */
void assert_free_list_size(int index, int size) {
    int cnt = 0;
    sf_block *bp = sf_free_list_heads[index].body.links.next;
    while(bp != &sf_free_list_heads[index]) {
	cnt++;
	bp = bp->body.links.next;
    }
    cr_assert_eq(cnt, size, "Free list %d has wrong number of free blocks (exp=%d, found=%d)",
		 index, size, cnt);
}

Test(sf_memsuite_student, malloc_an_int, .init = sf_mem_init, .fini = sf_mem_fini) {
	sf_errno = 0;
	int *x = sf_malloc(sizeof(int));

	cr_assert_not_null(x, "x is NULL!");

	*x = 4;

	cr_assert(*x == 4, "sf_malloc failed to give proper space for an int!");

	assert_free_block_count(0, 1);
	assert_free_block_count(3904, 1);
	assert_free_list_size(NUM_FREE_LISTS-1, 1);

	cr_assert(sf_errno == 0, "sf_errno is not zero!");
	cr_assert(sf_mem_start() + PAGE_SZ == sf_mem_end(), "Allocated more than necessary!");
}

Test(sf_memsuite_student, malloc_three_pages, .init = sf_mem_init, .fini = sf_mem_fini) {
	sf_errno = 0;
	// We want to allocate up to exactly three pages.
	void *x = sf_malloc(3 * PAGE_SZ - ((1 << 6) - sizeof(sf_header)) - 64 - 2*sizeof(sf_header));

	cr_assert_not_null(x, "x is NULL!");
	assert_free_block_count(0, 0);
	cr_assert(sf_errno == 0, "sf_errno is not 0!");
}

Test(sf_memsuite_student, malloc_too_large, .init = sf_mem_init, .fini = sf_mem_fini) {
	sf_errno = 0;
	void *x = sf_malloc(PAGE_SZ << 16);

	cr_assert_null(x, "x is not NULL!");
	assert_free_block_count(0, 1);
	assert_free_block_count(65408, 1);
	cr_assert(sf_errno == ENOMEM, "sf_errno is not ENOMEM!");
}

Test(sf_memsuite_student, free_quick, .init = sf_mem_init, .fini = sf_mem_fini) {
	sf_errno = 0;
	/* void *x = */ sf_malloc(8);
	void *y = sf_malloc(32);
	/* void *z = */ sf_malloc(1);

	sf_free(y);

	assert_free_block_count(0, 2);
	assert_free_block_count(64, 1);
	assert_free_block_count(3776, 1);
	cr_assert(sf_errno == 0, "sf_errno is not zero!");
}

Test(sf_memsuite_student, free_no_coalesce, .init = sf_mem_init, .fini = sf_mem_fini) {
	sf_errno = 0;
	/* void *x = */ sf_malloc(8);
	void *y = sf_malloc(200);
	/* void *z = */ sf_malloc(1);

	sf_free(y);

	assert_free_block_count(0, 2);
	assert_free_block_count(256, 1);
	assert_free_block_count(3584, 1);
	cr_assert(sf_errno == 0, "sf_errno is not zero!");
}

Test(sf_memsuite_student, free_coalesce, .init = sf_mem_init, .fini = sf_mem_fini) {
	sf_errno = 0;
	/* void *w = */ sf_malloc(8);
	void *x = sf_malloc(200);
	void *y = sf_malloc(300);
	/* void *z = */ sf_malloc(4);

	sf_free(y);
	sf_free(x);

	assert_free_block_count(0, 2);
	assert_free_block_count(576, 1);
	assert_free_block_count(3264, 1);
	cr_assert(sf_errno == 0, "sf_errno is not zero!");
}

Test(sf_memsuite_student, freelist, .init = sf_mem_init, .fini = sf_mem_fini) {
	void *u = sf_malloc(200);
	/* void *v = */ sf_malloc(300);
	void *w = sf_malloc(200);
	/* void *x = */ sf_malloc(500);
	void *y = sf_malloc(200);
	/* void *z = */ sf_malloc(700);

	sf_free(u);
	sf_free(w);
	sf_free(y);

	assert_free_block_count(0, 4);
	assert_free_block_count(256, 3);
	assert_free_block_count(1600, 1);
	assert_free_list_size(3, 3);
	assert_free_list_size(NUM_FREE_LISTS-1, 1);

	// First block in list should be the most recently freed block.
	int i = 3;
	sf_block *bp = sf_free_list_heads[i].body.links.next;
	cr_assert_eq(bp, (char *)y - 2*sizeof(sf_header),
		     "Wrong first block in free list %d: (found=%p, exp=%p)",
                     i, bp, (char *)y - 2*sizeof(sf_header));
}

Test(sf_memsuite_student, realloc_larger_block, .init = sf_mem_init, .fini = sf_mem_fini) {
	void *x = sf_malloc(sizeof(int));
	/* void *y = */ sf_malloc(10);
	x = sf_realloc(x, sizeof(int) * 20);

	cr_assert_not_null(x, "x is NULL!");
	sf_block *bp = (sf_block *)((char *)x - 2*sizeof(sf_header));
	cr_assert(bp->header & THIS_BLOCK_ALLOCATED, "Allocated bit is not set!");
	cr_assert((bp->header & BLOCK_SIZE_MASK) == 128, "Realloc'ed block size not what was expected!");

	assert_free_block_count(0, 2);
	assert_free_block_count(64, 1);
	assert_free_block_count(3712, 1);
}

Test(sf_memsuite_student, realloc_smaller_block_splinter, .init = sf_mem_init, .fini = sf_mem_fini) {
	void *x = sf_malloc(sizeof(int) * 20);
	void *y = sf_realloc(x, sizeof(int) * 16);

	cr_assert_not_null(y, "y is NULL!");
	cr_assert(x == y, "Payload addresses are different!");

	sf_block *bp = (sf_block *)((char*)y - 2*sizeof(sf_header));
	cr_assert(bp->header & THIS_BLOCK_ALLOCATED, "Allocated bit is not set!");
	cr_assert((bp->header & BLOCK_SIZE_MASK) == 128, "Block size not what was expected!");

	// There should be only one free block of size 3840.
	assert_free_block_count(0, 1);
	assert_free_block_count(3840, 1);
}

Test(sf_memsuite_student, realloc_smaller_block_free_block, .init = sf_mem_init, .fini = sf_mem_fini) {
	void *x = sf_malloc(sizeof(double) * 8);
	void *y = sf_realloc(x, sizeof(int));

	cr_assert_not_null(y, "y is NULL!");

	sf_block *bp = (sf_block *)((char*)y - 2*sizeof(sf_header));
	cr_assert(bp->header & THIS_BLOCK_ALLOCATED, "Allocated bit is not set!");
	cr_assert((bp->header & BLOCK_SIZE_MASK) == 64, "Realloc'ed block size not what was expected!");

	// After realloc'ing x, we can return a block of size 64 to the freelist.
	// This block will go into the main freelist and be coalesced.
	assert_free_block_count(0, 1);
	assert_free_block_count(3904, 1);
}

//############################################
//STUDENT UNIT TESTS SHOULD BE WRITTEN BELOW
//DO NOT DELETE THESE COMMENTS
//############################################

Test(sf_memsuite_student, memalign_first_test, .init = sf_mem_init, .fini = sf_mem_fini) {
	//debug("---OWN TEST 1---");
	//sf_malloc(8);
	void *x = sf_memalign(500, 256);
	cr_assert(((long int)x) % 256 == 0, "Block not alligned properly!");
	sf_malloc(800);
	void *y = sf_memalign(700, 512);
	cr_assert(((long int)y) % 512 == 0, "Block not alligned properly!");
	//sf_show_heap();
}

Test(sf_memsuite_student, malloc_split_freed_block, .init = sf_mem_init, .fini = sf_mem_fini) {
	//debug("---OWN TEST 2---");

	void *x = sf_malloc(500);
	sf_malloc(50);
	sf_malloc(110);
	sf_free(x);
	// check footer
	//sf_show_heap();
	void *y = sf_malloc(120);
	cr_assert(x == y, "Split block lower part is in wrong place!");
	assert_free_block_count((512 - 128), 1);
	assert_free_block_count(3264, 1);
	//sf_show_heap();
	sf_malloc(290);
	assert_free_block_count((384 - 320), 1);
	assert_free_block_count(3264, 1);
	//sf_show_heap();
	sf_malloc(50);
	assert_free_block_count(3264, 1);
	//sf_show_heap();
}

Test(sf_memsuite_student, coalesce_three_free_blocks, .init = sf_mem_init, .fini = sf_mem_fini) {
	//debug("---OWN TEST 3---");

	void *a = sf_malloc(4);
	void *b = sf_malloc(130);
	void *c = sf_malloc(100);
	void *n = sf_malloc(8);
	sf_malloc(16);
	sf_free(a);
	sf_free(c);
	//sf_show_heap();
	sf_free(b);
	assert_free_block_count(384, 1);
	sf_block *bp = (sf_block *)((char*)a - 2*sizeof(sf_header));
	cr_assert(!(bp->header & THIS_BLOCK_ALLOCATED), "Allocated bit is not set to free!");
	cr_assert((bp->header & BLOCK_SIZE_MASK) == 384, "Realloc'ed block size not what was expected!");
	cr_assert(bp->header & PREV_BLOCK_ALLOCATED, "Previous allocated bit is not set!");
	sf_block *footer = (sf_block *)((void *)(bp) + 384 - sizeof(sf_footer));
	cr_assert(!(footer->header & THIS_BLOCK_ALLOCATED), "Allocated bit is not set to free!");
	cr_assert((footer->header & BLOCK_SIZE_MASK) == 384, "Realloc'ed block size not what was expected!");
	cr_assert(footer->header & PREV_BLOCK_ALLOCATED, "Previous allocated bit is not set!");
	sf_block *next_prev_footer = (sf_block *)((char*)n - 3*sizeof(sf_header));
	//sf_show_block(bp);
	cr_assert(next_prev_footer == footer, "Freed block footer is in wrong position!");
	//sf_show_heap();
}

Test(sf_memsuite_student, realloc_same_size, .init = sf_mem_init, .fini = sf_mem_fini) {
	//debug("---OWN TEST 4---");

	sf_malloc(100);
	void *x = sf_malloc(400);
	//sf_show_heap();
	void *y = sf_realloc(x, 400);
	cr_assert(x == y, "Realloced payload pointer is in wrong place!");
	void *z = sf_realloc(x, 399);
	cr_assert(y == z, "Realloced payload pointer is in wrong place!");
	//sf_show_heap();
	sf_block *bp = (sf_block *)((char*)z - 2*sizeof(sf_header));
	cr_assert(bp->header & THIS_BLOCK_ALLOCATED, "Allocated bit is not set!");
	cr_assert((bp->header & BLOCK_SIZE_MASK) == 448, "Realloc'ed block size not what was expected!");
	cr_assert(bp->header & PREV_BLOCK_ALLOCATED, "Previous allocated bit is not set!");
}

Test(sf_memsuite_student, no_wilderness, .init = sf_mem_init, .fini = sf_mem_fini) {
	//debug("---OWN TEST 5---");

	sf_malloc(3960); // 3968
	//sf_show_heap();
	assert_free_list_size(NUM_FREE_LISTS-1, 0);
	sf_malloc(203);
	sf_malloc(30);
	//sf_show_heap();
	assert_free_list_size(NUM_FREE_LISTS-1, 1);
	assert_free_block_count(3776, 1);
	sf_malloc(3768);
	//sf_show_heap();
	assert_free_list_size(NUM_FREE_LISTS-1, 0);
}

Test(sf_memsuite_student, check_errno_and_nulls, .init = sf_mem_init, .fini = sf_mem_fini) {
	//debug("---OWN TEST 6---");

	// malloc
	void *x = sf_malloc(0);
	cr_assert(x == NULL, "Malloc did not return NULL for 0!");
	sf_errno = 0;
	cr_assert(sf_errno == 0, "sf_errno is not zero!");
	void *z = sf_malloc(1234567891011);
	cr_assert(z == NULL, "Malloc did not return NULL very large payload request!");
	cr_assert(sf_errno == ENOMEM, "sf_errno is not ENOMEM!");
	//sf_show_heap();

	// free
	// should abort the program with invalid pointer
	// sf_free(NULL);

	// realloc (ptr, rsize)
	sf_errno = 0;
	// size 0, should free and return NULL, without setting sf_errno
	void *y = sf_malloc(100);
	void *ptr = sf_malloc(30);
	void *w = sf_realloc(y, 0);
	cr_assert(w == NULL, "Realloc did not return NULL when size request is 0!");
	cr_assert(sf_errno == 0, "sf_errno was set to something when it shouldn't be set!");
	assert_free_block_count(128, 1);
	//sf_show_heap();

	// invalid pointer, NULL returned and sf_errno = EINVAL
	sf_errno = 0;
	// pointer is NULL
	void *p = NULL;
	void *check = sf_realloc(p, 64);
	cr_assert(check == NULL, "Realloc did not return NULL when pointer is NULL!");
	cr_assert(sf_errno == EINVAL, "sf_errno was set to something when it shouldn't be set!");
	sf_errno = 0;
	// pointer not alligned to a 64-byte boundary
	void *pptr = (void *)ptr + 3;
	check = sf_realloc(pptr, 64);
	cr_assert(check == NULL, "Realloc did not return NULL when pointer is NULL!");
	cr_assert(sf_errno == EINVAL, "sf_errno was set to something when it shouldn't be set!");
	sf_errno = 0;
	// allocated bit in the header is 0
	check = sf_realloc(y, 50);
	cr_assert(check == NULL, "Realloc did not return NULL when pointer is NULL!");
	cr_assert(sf_errno == EINVAL, "sf_errno was set to something when it shouldn't be set!");
	sf_errno = 0;
	// header of the block is before the end of the prologue,
	sf_block *prologue = (sf_block *)((void *)sf_mem_start() + (sizeof(sf_header) * 6)); // 48
	check = sf_realloc(prologue, 50);
	cr_assert(check == NULL, "Realloc did not return NULL when pointer is NULL!");
	cr_assert(sf_errno == EINVAL, "sf_errno was set to something when it shouldn't be set!");
	// or footer of the block is after the beginning of the epilogue
	// prev_alloc field is 0, alloc field of the previous block header is not 0
	// if no memory available sf_realloc, sf_errno = ENOMEM
	sf_errno = 0;
	sf_malloc(65216 - 8);
	//sf_show_heap();
	check = sf_realloc(ptr, 999);
	cr_assert(check == NULL, "Realloc did not return NULL when pointer is NULL!");
	cr_assert(sf_errno == ENOMEM, "sf_errno was set to something when it shouldn't be set!");

	// memalign (size, align)
	sf_errno = 0;
	void *a = sf_memalign(1, 2); // less than min block size
	cr_assert(a == NULL, "Memalign did not return NULL for align smaller than minimum request!");
	cr_assert(sf_errno == EINVAL, "sf_errno is not EINVAL!");
	sf_errno = 0;
	void *b = sf_memalign(1, 111); // not a power of 2
	cr_assert(b == NULL, "Memalign did not return NULL for align not power of 2 request!");
	cr_assert(sf_errno == EINVAL, "sf_errno is not EINVAL!");

	// if allocation not successful, NULL is returned and sf_errno = ENOMEM
}
/*
Test(sf_memsuite_student, multiple_frees, .init = sf_mem_init, .fini = sf_mem_fini) {
	debug("---OWN TEST 7---");

	sf_malloc(1);
	void * a = sf_malloc(332);
	sf_malloc(654);
	void *b = sf_malloc(2);
	sf_malloc(100);
	void *c = sf_malloc(3);
	sf_malloc(100);
	void *d = sf_malloc(4);
	sf_malloc(5);
	sf_free(a);
	sf_free(b);
	sf_free(c);
	sf_free(d);
	//sf_show_heap();
}
*/
/*
Test(sf_memsuite_student, testing, .init = sf_mem_init, .fini = sf_mem_fini) {
	debug("---OWN TEST 8---");

	sf_memalign(4, 1024);
	sf_memalign(4, 512);
	sf_show_heap();
}
*/