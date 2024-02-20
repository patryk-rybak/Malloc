# Mallco

### The code I wrote is located in mm.c.


### GENERAL INFORMATION

Block size is a multiple of 16 bytes (`ALIGNMENT`). Each block has a header
containing information about its size and two flags, the first about whether the
block is free or allocated and the second about the same thing but in the
context of the previous block. Free blocks additionally have pointers for the
previous and next free block and a footer with the same information as the
header. They are organized in a segregated list that contains `N_BUCKTES` buckets
that separate the blocks by appropriate size. The user's payload starts at an
address divisible by 16.


1. ### DESCRIPTION OF THE BLOCK STRUCTURE

Each block contains a 4-byte header with information about the size of the
entire block (the size is in words and is divisible by 16). On the last two bits
there are flags indicating whether the block is used or free and the same about
its previous friend. If there is no previous block, the flag is set as if it was
used.

Free blocks additionally have pointers for the previous and next free block in
the list of free blocks and a footer, which contains the same information as the
header. Pointers are not actually real pointers, but the distance from the
beginning of the heap (internal fragmentation optimization). If a free block
needs to represent the absence of a predecessor or successor, it does so with
the -1 value.


2. ### DESCRIPTION OF MEMORY ALLOCATION AND FREEING

The `malloc` procedure adds the header size to the received one and rounds it to
the nearest number divisible by 16. Then it searches the segregated list of free
blocks to find a block of the designated size or larger using the first fit
strategy. The found block is removed from the list and its metadata is changed.
If it turns out that the found block is larger than the required size and the
unnecessary part is >= 16 bytes, this part is separated and a new free block
with appropriate metadata is created from it, which is added to the list of free
blocks.

The `free` procedure changes the block's metadata, marking it as free, then
checks whether any of its neighboring firends are also free. If so, the free
blocks are connected together. The block is added to the appropriate free block
list.


3. ### ORGANIZATION OF THE FREE BLOCKS LIST

To manage free blocks I use segregated lists with `N_BUCKETS` (10) buckets. Each
bucket is a pointer to the first element of the block list with sizes in the
appropriate range. The ranges are:

2^4, (2^4, 2^5], (2^6, 2^7], ..., (2^(N_BUCKETS + 1), 2^(N_BUCKETS + 3)],
(2^(N_BUCKETS + 3), +inf)

Adding and removing elements from buckets is done according to the LIFO
principle.


##
[detailed project description](projekt-malloc.pdf)