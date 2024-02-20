/*

author: Patryk Rybak 333139


GENERAL INFORMATION

Block size is a multiple of 16 bytes (ALIGNMENT). Each block has a header
containing information about its size and two flags, the first about whether the
block is free or allocated and the second about the same thing but in the
context of the previous block. Free blocks additionally have pointers for the
previous and next free block and a footer with the same information as the
header. They are organized in a segregated list that contains N_BUCKTES buckets
that separate the blocks by appropriate size. The user's payload starts at an
address divisible by 16.



DESCRIPTION OF THE BLOCK STRUCTURE

Each block contains a 4-byte header with information about the size of the
entire block (the size is in words and is divisible by 16). On the last two bits
there are flags indicating whether the block is used or free and the same about
its previous friend. If there is no previous block, the flag is set as if it was
used.

Free blocks additionally have pointers for the previous and next free block in
the list of free blocks and a footer, which contains the same information as the
header. Pointers are not actually real pointers, but the distance from the
beginning of the heap. If a free block needs to represent the absence of a
predecessor or successor, it does so with the -1 value.



DESCRIPTION OF MEMORY ALLOCATION AND FREEING

The <malloc> procedure adds the header size to the received one and rounds it to
the nearest number divisible by 16. Then it searches the segregated list of free
blocks to find a block of the designated size or larger using the first fit
strategy. The found block is removed from the list and its metadata is changed.
If it turns out that the found block is larger than the required size and the
unnecessary part is >= 16 bytes, this part is separated and a new free block
with appropriate metadata is created from it, which is added to the list of free
blocks.

The <free> procedure changes the block's metadata, marking it as free, then
checks whether any of its neighboring firends are also free. If so, the free
blocks are connected together. The block is added to the appropriate free block
list.



ORGANIZATION OF THE FREE BLOCKS LIST

To manage free blocks I use segregated lists with N_BUCKETS (10) buckets. Each
bucket is a pointer to the first element of the block list with sizes in the
appropriate range. The ranges are:

2^4, (2^4, 2^5], (2^6, 2^7], ..., (2^(N_BUCKETS + 1), 2^(N_BUCKETS + 3)],
(2^(N_BUCKETS + 3), +inf)

Adding and removing elements from buckets is done according to the LIFO
principle.

*/

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <unistd.h>

#include "mm.h"
#include "memlib.h"

/* If you want debugging output, use the following macro.  When you hand
 * in, remove the #define DEBUG line. */
// #define debug
#ifdef debug
#define debug(fmt, ...) printf("%s: " fmt "\n", __func__, __va_args__)
#define msg(...) printf(__VA_ARGS__)
#else
#define debug(fmt, ...)
#define msg(...)
#endif

/* do not change the following! */
#ifdef DRIVER
/* create aliases for driver tests */
#define malloc mm_malloc
#define free mm_free
#define realloc mm_realloc
#define calloc mm_calloc
#endif /* def DRIVER */

/* Pack a size and allocated bit into a word */
#define PACK(size, alloc) ((size) | (alloc)) // CSAPP

/* write a word at address p */
#define PUT(p, val) (*(unsigned int *)(p) = (val)) // CSAPP

typedef int32_t word_t;      /* Heap is bascially an array of 4-byte words. */
#define WSIZE sizeof(word_t) /* Size of word in bytes */
#define MINBSIZE                                                               \
  16 / WSIZE         /* Blocks have to be minimum 16 bytes it is 4 words */
#define N_BUCKETS 10 /* Number of buckets */

typedef enum {
  FREE = 0,     /* Block is free */
  USED = 1,     /* Block is used */
  PREVFREE = 2, /* Previous block is free (boundary tags optimalization) */
} bt_flags;

static word_t *heap_start;    /* Address of the first block */
static word_t *heap_epilogue; /* Addres of the epilogue */
static word_t *last;          /* Points at the begigning if the last block */
static word_t *
  *segregated_list; /* Array of all free lists of free blocks (buckets)*/

static size_t round_up(size_t size) {
  return (size + ALIGNMENT - 1) & -ALIGNMENT;
}

static inline word_t bt_size(word_t *bt) { // mm-implicit.c
  return *bt & ~(USED | PREVFREE);
}

static inline int bt_used(word_t *bt) { // mm-implicit.c
  return *bt & USED;
}

/* Previous block free flag handling for optimized boundary tags. */
static inline bt_flags bt_get_prevfree(word_t *bt) { // mm-implicit.c
  return *bt & PREVFREE;
}

static inline void bt_clr_prevfree(word_t *bt) { // mm-implicit.c
  if (bt)
    *bt &= ~PREVFREE;
}

static inline void bt_set_prevfree(word_t *bt) { // mm-implicit.c
  *bt |= PREVFREE;
}

/* Returns address of payload. */
static inline void *bt_payload(word_t *bt) { // mm-implicit.c
  return bt + 1;
}

/* Given boundary tag address calculate it's buddy address. */
static inline word_t *bt_footer(word_t *bt) {
  return bt + bt_size(bt) - 1;
}

/* Returns address of next block or NULL. */
static inline word_t *bt_next(word_t *bt) {
  word_t *next = bt_footer(bt) + 1;
  return (next == heap_epilogue) ? NULL : next;
}

/* Returns address of previous block or NULL. */
static inline word_t *bt_prev(word_t *bt) {
  if (bt_get_prevfree(bt)) {
    word_t *prev = bt - bt_size(bt - 1);
    return prev;
  }
  return NULL;
}

/* Creates boundary tag(s) for given block. */
static inline void bt_make(word_t *bt, word_t words, bt_flags flags) {
  PUT(bt, PACK(words, flags));
  if (bt_next(bt)) {
    if (bt_used(bt)) {
      bt_clr_prevfree(bt_next(bt));
      return;
    } else {
      bt_set_prevfree(bt_next(bt));
    }
  }
  PUT(bt_footer(bt), PACK(words, flags));
}

/*
 * mm_init - Called when a new trace starts.
 */
int mm_init(void) {

  /* taking the place for pointers corresponding to buckets */
  if ((segregated_list = mem_sbrk(N_BUCKETS * sizeof(heap_start))) == NULL)
    return -1;

  /* alignment */
  void *temp;
  if ((temp = mem_sbrk(0)) == NULL)
    return -1;
  size_t remainder = (uintptr_t)temp % 16;

  /* alignment */
  size_t size = (remainder > 12) ? 16 - remainder - 12 : 12 - remainder;

  /* alignment */
  if (mem_sbrk(size) == NULL)
    return -1;

  /* epilogue */
  heap_start = heap_epilogue = (word_t *)mem_sbrk(WSIZE);

  /* setting header in epilogue */
  PUT(heap_epilogue, USED);

  last = NULL;

  /* setting buckets pointers */
  for (int i = 0; i < N_BUCKETS; i++)
    segregated_list[i] = heap_start - 1;

  return 0;
}

/*
 * free list API
 */
static inline int find_bucket(word_t words) {
  size_t size = words * WSIZE;
  size_t boundary = 16;
  int index = 0;
  do {
    if (size <= boundary) {
      assert(size <= boundary && size > (boundary >> 1) &&
             "blok w zlym kubelku");
      return index;
    }
    index++;
    boundary <<= 1;

  } while (index < N_BUCKETS - 1);
  return N_BUCKETS - 1;
}

static inline void set_free_list_prev(word_t *bt, word_t *free_prev) {
  PUT(bt + 2, (word_t)(free_prev - heap_start));
}

static inline void set_free_list_next(word_t *bt, word_t *free_next) {
  PUT(bt + 1, (word_t)(free_next - heap_start));
}

static inline word_t *get_free_list_prev(word_t *bt) {
  return (*(bt + 2) < 0) ? NULL : heap_start + *(bt + 2);
}

static inline word_t *get_free_list_next(word_t *bt) {
  return ((word_t) * (bt + 1) < 0) ? NULL : heap_start + *(bt + 1);
}

static inline void free_list_append(word_t *bt) {

  int index = find_bucket(bt_size(bt));

  set_free_list_prev(bt, heap_start - 1);
  set_free_list_next(bt, segregated_list[index]);

  segregated_list[index] = bt;

  if (get_free_list_next(bt))
    set_free_list_prev(get_free_list_next(bt), bt);
}

// free block : [ headr | next | prev | ... | footer ]
static inline void free_list_delete(word_t *bt) {

  int index = find_bucket(bt_size(bt));

  // block is the only one in the list
  if (segregated_list[index] == bt && get_free_list_next(bt) == NULL) {
    segregated_list[index] = heap_start - 1;
  }
  // block is the first one but not the last
  else if (segregated_list[index] == bt) {
    segregated_list[index] = get_free_list_next(bt);
    set_free_list_prev(segregated_list[index], heap_start - 1);
  }
  // block is somewhere in the middle of the list
  else if (get_free_list_next(bt) != NULL) {
    set_free_list_next(get_free_list_prev(bt), get_free_list_next(bt));
    set_free_list_prev(get_free_list_next(bt), get_free_list_prev(bt));
  }
  // block is the last one in the list
  else {
    set_free_list_next(get_free_list_prev(bt), heap_start - 1);
  }
}

/*
 * coalesce - If possible, it combines adjacent free blocks into one
 * 	and adds it to the list of free blocks.
 */
static word_t *coalesce(word_t *bt) {

  /* this is NULL if the previous block is used */
  word_t *prev = bt_prev(bt);
  /* this is NULL if the next block is epilogue */
  word_t *next = bt_next(bt);

  int prev_used = (prev) ? bt_used(prev) : 1;
  int next_used = (next) ? bt_used(next) : 1;

  word_t words = bt_size(bt);

  int is_change = (bt == last || (next == last && !next_used) ? 1 : 0);

  if (!next_used) {
    words += bt_size(next);
    free_list_delete(next);
  }

  if (!prev_used) {
    words += bt_size(prev);
    free_list_delete(prev);
    bt = prev;
  }

  bt_make(bt, words, FREE);
  free_list_append(bt);

  last = (is_change) ? bt : last;

  return bt;
}

/*
 * extend_heap
 */
static word_t *extend_heap(size_t size) {
  word_t *bt;
  bt_flags flags = 0;
  if ((void *)mem_sbrk(size) == (void *)-1)
    return NULL;

  /* old epilogue becomes begining of the new block */
  bt = heap_epilogue;

  /* checking if old last block is free */
  if (last && !(bt_used(last)))
    flags |= PREVFREE;
  flags |= FREE;

  bt_make(bt, size / WSIZE, flags);

  last = bt;

  /* epilogue */
  PUT(heap_epilogue = bt_footer(bt) + 1, PACK(0, USED));
  assert((uintptr_t)heap_epilogue % 16 == 12 &&
         "extend_heap niewyrownany epilogue");

  /* coalescing with old last block in case it is free */
  return coalesce(bt);
}

/*
 * place - Places metadata on the block, marks it as used and, if possible,
 * 	creates a new free block and adds it to the list of free blocks
 */
static void place(void *bt, word_t words_needed) {

  word_t free_block_words = bt_size(bt);
  free_list_delete(bt);

  /* setting the block to used and creating new free block grom the leftovers*/
  if (free_block_words - words_needed >= ALIGNMENT) {
    bt_make(bt, words_needed, USED | bt_get_prevfree(bt));

    word_t *remaining_block = bt_next(bt);
    bt_make(remaining_block, free_block_words - words_needed, FREE);
    free_list_append(remaining_block);

    last = (last == bt) ? remaining_block : last;

    /* setting the block to used */
  } else {
    bt_make(bt, free_block_words, USED);
  }
}

/*
 * find_fit - Searches for a free block of the given size or larger using first
 * fit startegy. Searches bucket by bucket, increasing block sizes. Skips empty
 * buckets.
 */
static word_t *find_fit(word_t words) {
  int index = find_bucket(words);
  word_t *bt;
  do {
    /* skips empty buckets */
    while ((segregated_list[index] - heap_start == -1) && (index < N_BUCKETS))
      index++;

    bt = segregated_list[index];

    /* searching in the selected bucket */
    while (bt != NULL) {
      if (bt_size(bt) >= words)
        return bt;

      bt = get_free_list_next(bt);
    }
    index++;

  } while (index < N_BUCKETS);
  return NULL;
}

/*
 * malloc - Allocate a block by incrementing the brk pointer.
 *      Always allocate a block whose size is a multiple of the alignment.
 */
void *malloc(size_t size) {
  word_t *bt;

  if (!size)
    return NULL;

  /* headr + playoad + padding (in bytes) */
  size = round_up(WSIZE + size);
  /* headr + playoad + padding (in words) */
  word_t words = size / WSIZE;

  /* Search the free list for a fit */
  if ((bt = find_fit(words)) != NULL) {
    place(bt, words);
    return (void *)(bt + 1);
  }

  // size (bytes) needed to extend_heap
  size_t needed = size;

  if (last != NULL && !bt_used(last))
    needed = size - bt_size(last) * WSIZE;

  if ((bt = extend_heap(needed)) == NULL)
    return NULL;

  place(bt, words);

  return (void *)(bt + 1);
}

/*
 * free - We don't know how to free a block.  So we ignore this call.
 *      Computers have big memories; surely it won't be a problem.
 */
void free(void *ptr) {

  if (!ptr)
    return;

  word_t *bt = (word_t *)ptr - 1;

  bt_flags flags = FREE | bt_get_prevfree(bt);

  bt_make(bt, bt_size(bt), flags);

  /* coalescing free neighbors */
  if (bt_get_prevfree(bt) || (bt_next(bt) && bt_used(bt_next(bt))))
    coalesce(bt);
  else
    free_list_append(bt);
}

/*
 * realloc - Change the size of the block by mallocing a new block,
 *      copying its data, and freeing the old block.
 **/
void *realloc(void *old_ptr, size_t size) {

  if (size == 0) {
    free(old_ptr);
    return NULL;
  }

  if (!old_ptr)
    return malloc(size);

  word_t *bt = (word_t *)old_ptr - 1;

  void *new_ptr = malloc(size);
  if (!new_ptr)
    return NULL;

  size_t free_size = bt_size((word_t *)new_ptr - 1) * WSIZE;
  size_t old_size = bt_size(bt) * WSIZE;

  /* Copy the old data. */
  if (free_size > old_size)
    memcpy(new_ptr, old_ptr, old_size - WSIZE);
  else
    memcpy(new_ptr, old_ptr, free_size - WSIZE);

  /* Free the old block. */
  free(old_ptr);

  return new_ptr;
}

/*
 * calloc - Allocate the block and set it to zero.
 */
void *calloc(size_t nmemb, size_t size) {

  size_t bytes = nmemb * size;
  void *new_ptr = malloc(bytes);

  /* If malloc() fails, skip zeroing out the memory. */
  if (new_ptr)
    memset(new_ptr, 0, bytes);

  return new_ptr;
}

/*
 * mm_checkheap - So simple, it doesn't need a checker!
 */
void mm_checkheap(int verbose) {
  msg("ok\n");
}
