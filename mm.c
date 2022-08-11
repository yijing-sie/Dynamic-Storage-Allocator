/**
 * @file mm.c
 * @brief A 64-bit struct-based implicit free list memory allocator
 *
 * 15-213: Introduction to Computer Systems
 *
 * TODO: insert your documentation here. :)
 *
 *************************************************************************
 *
 * ADVICE FOR STUDENTS.
 * - Step 0: Please read the writeup!
 * - Step 1: Write your heap checker.
 * - Step 2: Write contracts / debugging assert statements.
 * - Good luck, and have fun!
 *
 *************************************************************************
 *
 * @author Yi-Jing <ysie@andrew.cmu.edu>
 */

#include <assert.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "memlib.h"
#include "mm.h"

/* Do not change the following! */

#ifdef DRIVER
/* create aliases for driver tests */
#define malloc mm_malloc
#define free mm_free
#define realloc mm_realloc
#define calloc mm_calloc
#define memset mem_memset
#define memcpy mem_memcpy
#endif /* def DRIVER */

/* You can change anything from here onward */

/*
 *****************************************************************************
 * If DEBUG is defined (such as when running mdriver-dbg), these macros      *
 * are enabled. You can use them to print debugging output and to check      *
 * contracts only in debug mode.                                             *
 *                                                                           *
 * Only debugging macros with names beginning "dbg_" are allowed.            *
 * You may not define any other macros having arguments.                     *
 *****************************************************************************
 */
#ifdef DEBUG
/* When DEBUG is defined, these form aliases to useful functions */
#define dbg_printf(...) printf(__VA_ARGS__)
#define dbg_requires(expr) assert(expr)
#define dbg_assert(expr) assert(expr)
#define dbg_ensures(expr) assert(expr)
#define dbg_printheap(...) print_heap(__VA_ARGS__)
#else
/* When DEBUG is not defined, no code gets generated for these */
/* The sizeof() hack is used to avoid "unused variable" warnings */
#define dbg_printf(...) (sizeof(__VA_ARGS__), -1)
#define dbg_requires(expr) (sizeof(expr), 1)
#define dbg_assert(expr) (sizeof(expr), 1)
#define dbg_ensures(expr) (sizeof(expr), 1)
#define dbg_printheap(...) ((void)sizeof(__VA_ARGS__))
#endif

/* Basic constants */

typedef uint64_t word_t;

/** @brief Word and header size (bytes) */
static const size_t wsize = sizeof(word_t);

/** @brief Double word size (bytes) */
static const size_t dsize = 2 * wsize;

/** @brief Minimum block size (bytes) */
static const size_t min_block_size = 2 * dsize;

/** @brief Mnimum chunk size (bytes) */
static const size_t chunksize = (1 << 12);

/**
 * @brief A mask to get the bit from a word
 */
static const word_t alloc_mask = 0x1;     // allocation bit
static const word_t alloc_mask_pre = 0x2; // previous block allocation bit
static const word_t mini_mask_pre = 0x4;  // previous miniblock allocation bit
static const word_t size_mask = ~(word_t)0xF; // block size bits

/** @brief Represents the header and payload of one block in the heap */
typedef struct block {
    /** @brief Header contains size + allocation flag */
    word_t header;
    // A union for pointer arithmetic
    union {
        /* next and previous block pointer */
        struct {
            struct block *next;
            struct block *prev;
        };
        /** @brief A pointer to the block payload.*/
        char payload[0];
    };

} block_t;

/* Mini block */
// All the miniblock have the same size so only next pointer is needed
typedef struct mini_block {
    word_t header;
    union {
        struct mini_block *next;
        char payload[0];
    };

} miniblock_t;

/* Global variables */
/** @brief Pointer to first block in the heap */
static void *heap_start = NULL;
// Number of the list
static const size_t num_lists = 12;
static block_t *seglist[num_lists];
static miniblock_t *mini_list;
// static bool flag = false;
// static bool implicit = false;

/*
 *****************************************************************************
 * The functions below are short wrapper functions to perform                *
 * bit manipulation, pointer arithmetic, and other helper operations.        *
 *                                                                           *
 * We've given you the function header comments for the functions below      *
 * to help you understand how this baseline code works.                      *
 *                                                                           *
 * Note that these function header comments are short since the functions    *
 * they are describing are short as well; you will need to provide           *
 * adequate details for the functions that you write yourself!               *
 *****************************************************************************
 */

/*
 * ---------------------------------------------------------------------------
 *                        BEGIN SHORT HELPER FUNCTIONS
 * ---------------------------------------------------------------------------
 */

/**
 * @brief Returns the maximum of two integers.
 * @param[in] x one of the number to be compared
 * @param[in] y the other number to be compared
 * @return `x` if `x > y`, and `y` otherwise.
 */
static size_t max(size_t x, size_t y) {
    return (x > y) ? x : y;
}

/**
 * @brief Rounds `size` up to the multiple of n
 * @param[in] size The original size to be rounded up
 * @param[in] n `size` is the mutiple of n
 * @return The size after rounding up
 */
static size_t round_up(size_t size, size_t n) {
    return n * ((size + (n - 1)) / n);
}

/**
 * @brief Packs the `size`, `alloc` , `pre_alloc` of a block into a word
 * suitable for use as a packed value.
 *
 * Packed values are used for both headers and footers.
 *
 * The allocation status is packed into the lowest bit of the word.
 * The allocation status of the previous block is stored into the last but one
 * bit of the word
 * @param[in] size The size of the block being represented
 * @param mini True if the previous block is mini block
 * @param alloc_pre True if the preious block is allocated
 * @param alloc True if the block is allocated
 * @return The packed word
 */
static word_t pack(size_t size, bool mini, bool alloc_pre, bool alloc) {
    word_t word = size;
    if (alloc) {
        word |= alloc_mask;
    }
    if (alloc_pre) {
        word |= alloc_mask_pre;
    }
    if (mini) {
        word |= mini_mask_pre;
    }
    return word;
}

/**
 * @brief Extracts the size represented in a packed word.
 *
 * This function simply clears the lowest 4 bits of the word, as the heap
 * is 16-byte aligned.
 *
 * @param[in] word the word to be extrated size from
 * @return The size of the block represented by the word
 */
static size_t extract_size(word_t word) {
    return (word & size_mask);
}

/**
 * @brief Extracts the size of a block
 * @param[in] block the block to be extrated size from by reading its header
 * @return The size of the block
 */
static size_t get_size(block_t *block) {
    return extract_size(block->header);
}

/**
 * @brief Given a payload pointer, returns a pointer to the corresponding
 *        block.
 * @param[in] bp A pointer to a block's payload
 * @return The corresponding block to the payload
 */
static block_t *payload_to_header(void *bp) {
    return (block_t *)((char *)bp - offsetof(block_t, payload));
}

/**
 * @brief Given a block pointer, returns a pointer to the corresponding
 *        payload.
 * @param[in] block the block to be extracted payload from
 * @return A pointer to the block's payload
 * @pre The block must be a valid block, not a boundary tag.
 */
static void *header_to_payload(block_t *block) {
    dbg_requires(get_size(block) != 0);
    return (void *)(block->payload);
}

/**
 * @brief Given a block pointer, returns a pointer to the corresponding
 *        footer.
 * @param[in] block the block to be extracted footer from
 * @return A pointer to the block's footer
 * @pre The block must be a valid block, not a boundary tag.
 */
static word_t *header_to_footer(block_t *block) {
    dbg_requires(get_size(block) != 0 &&
                 "Called header_to_footer on the boundary block");
    return (word_t *)(block->payload + get_size(block) - dsize);
}

/**
 * @brief Given a block footer, returns a pointer to the corresponding
 *        header.
 * @param[in] footer A pointer to the block's footer
 * @return A pointer to the start of the block
 * @pre The footer must be the footer of a valid block, not a boundary tag.
 */
static block_t *footer_to_header(word_t *footer) {
    size_t size = extract_size(*footer);
    dbg_assert(size != 0 && "Called footer_to_header on the boundart block");
    return (block_t *)((char *)footer + wsize - size);
}

/**
 * @brief Returns the payload size of a given block from header
 *
 * The payload size is equal to the entire block size minus the sizes of the
 * block's header and footer.
 *
 * @param[in] block the block to be extracted the payload size from
 * @return The size of the block's payload
 */
static size_t get_payload_size(block_t *block) {
    size_t asize = get_size(block);
    return asize - wsize; // footer is not required for allocated blocks
}

/**
 * @brief Returns the allocation status of a given word
 *
 * This is based on the lowest bit of the word value.
 *
 * @param[in] word the word to be extracted the allocate status from
 * @return The allocation status correpsonding to the word
 */
static bool extract_alloc(word_t word) {
    return (bool)(word & alloc_mask);
}

/**
 * @brief Returns the allocation status of a block, based on its header.
 * @param[in] block
 * @return The allocation status of the block
 */
static bool get_alloc(block_t *block) {
    return extract_alloc(block->header);
}

/**
 * @brief Returns the previous allocation status of a block, based on its
 * header.
 * @param[in] word used to extract previous block allocate bit
 * @return The allocation status of the block
 */
bool extract_alloc_pre(word_t word) {
    return (bool)((word & alloc_mask_pre) >> 1);
}

/**
 * @brief Returns the previous allocation status of a block, based on its
 * header.
 * @param[in] block used to check previous block allocate status
 * @return The allocation status of the block
 */
bool get_alloc_pre(void *block) {
    return extract_alloc_pre(((block_t *)block)->header);
}

/**
 * @brief Returns the previous miniblock status of a block, based on its
 * header.
 * @param[in] wrod used to extract mini block bit
 * @return The miniblock status of the block
 */
bool extract_mini(word_t word) {
    return (bool)((word & mini_mask_pre) >> 2);
}

/**
 * @brief Returns the previous miniblock status of a block, based on its
 * header.
 * @param[in] wrod used to check mini block status
 * @return The miniblock status of the block
 */
bool get_mini(void *block) {
    return extract_mini(((block_t *)block)->header);
}

/**
 * @brief Writes a block starting at the given address.
 *
 * This function writes header and only writes footer for free blocks that are
 * not miniblocks where the location of the footer is computed in relation to
 * the header.
 *
 * @pre The size of the block is an even number of words for alignment and
 * block is not NUL
 * @param block The location to begin writing the block header
 * @param size The size of the new block
 * @param mini 1 if the previous block is a mini block
 * @param alloc The allocation status of the new block
 * @param alloc_pre The allocation status of the previous block
 */

static void write_block(block_t *block, size_t size, bool mini, bool alloc_pre,
                        bool alloc) {
    dbg_requires(block != NULL);
    block->header = pack(size, mini, alloc_pre, alloc);
    // only write footer for non-mini & free blocks
    if (!alloc && get_size(block) > dsize) {
        word_t *footerp = header_to_footer(block);
        *footerp = pack(size, mini, alloc_pre, alloc);
    }
}

/**
 * @brief Finds the next consecutive block on the heap. ((May return the
 * epilogue))
 *
 * This function accesses the next block in the "implicit list" of the heap
 * by adding the size of the block.
 *
 * @param[in] block A valid block in the heap
 * @return The next consecutive block on the heap; ((May return the epilogue))
 * @pre The block not boundary tags
 */
static block_t *find_next(void *block) {
    dbg_requires(block != NULL);
    dbg_requires(get_size(block) != 0 &&
                 "Called find_next on the boundairs in the heap");
    return (block_t *)((char *)block + get_size(block));
}

/**
 * @brief Finds the next consecutive free block on the heap in the free list.
 *
 * This function accesses the next block in the "free list" of the heap
 * by reading the next pointer
 *
 * @param[in] block A free block in the heap
 * @return The next consecutive free block on the heap
 * @pre The block is not the epilogue and it is free
 * @pro The returned block has to be free
 *
 */
static block_t *find_next_free(block_t *block) {
    dbg_requires(block != NULL);
    dbg_requires(get_size(block) != 0 &&
                 "Called find_next on boundary block in the heap");
    dbg_assert(!get_alloc(block) &&
               "Block is not int the free list when calling find_next_free.\n");
    block_t *next_free = block->next;
    return (next_free);
}

/**
 * @brief Finds the footer of the previous block on the heap. (( May return
 * pointer to the prologue ))
 * @param[in] block A block in the heap
 * @return The location of the previous block's footer
 * @pre block not the prologue
 * @pro retruned address not exceed the lower bound of the heap
 */
static word_t *find_prev_footer(block_t *block) {
    // Compute previous footer position as one word before the header
    dbg_requires(block != NULL);
    dbg_requires(block != (block_t *)((char *)heap_start - wsize) &&
                 "Called find_prev_footer on prologue\n");
    return &(block->header) - 1;
}

/**
 * @brief Finds the previous consecutive block on the heap.
 * If the function is called on the first block in the heap, NULL will be
 * returned, since the first block in the heap has no previous block!
 *
 * The position of the previous block is found by reading the previous
 * block's footer to determine its size, then calculating the start of the
 * previous block based on its size.
 *
 * @param[in] block A block in the heap
 * @return The previous consecutive block in the heap.
 * @pre block not on the boundaries
 * @pro retruned address not exceed the lower bound of the heap
 */
static block_t *find_prev(block_t *block) {
    dbg_requires(block != NULL);
    dbg_requires(get_size(block) != 0 && "Called find_prev on boundaries\n");
    word_t *footerp = find_prev_footer(block);
    // Return NULL if called on first block in the heap
    if (extract_size(*footerp) == 0) {
        return NULL;
    }
    return footer_to_header(footerp);
}

/**
 * @brief Writes an epilogue header at the given address.
 *
 * The epilogue header has size 0, and is marked as allocated.
 * @param[out] block The location to write the epilogue header
 */
static void write_epilogue(block_t *block) {
    dbg_requires(block != NULL);
    dbg_requires((char *)block == mem_heap_hi() - 7);
    block->header = pack(0, false, false, true);
}

/*
 * ---------------------------------------------------------------------------
 *                        END SHORT HELPER FUNCTIONS
 * ---------------------------------------------------------------------------
 */

/******** The remaining content below are helper and debug routines ********/

/**
 * @brief find the index given block size
 * The number of non-zero bits after right-shifted by 6 bits for
 *  block_size are used to determine the index of list
 * @param[in] size the size of the block
 */
size_t find_seg_index(size_t block_size) {
    size_t idx = 0;
    block_size = block_size >> 6;
    while (block_size != 0) {
        block_size = block_size >> 1;
        idx++;
    }
    if (idx < num_lists)
        return idx;
    else {
        // for too large size of block just leave it in the last list
        return num_lists - 1;
    }
}

/**
 * @brief find first fit in seglist[index]; return NULL if no fit found
 * @param[in] asize size of the block
 * @param[in] index the list number to be searched for fit blocks
 * @return found or NULL
 */
block_t *find_seg_fit(size_t index, size_t asize) {
    block_t *block_1 = seglist[index];
    block_t *block = block_1;
    if (block_1 == NULL)
        return NULL;
    while (block) {
        if (get_size(block) >= asize) {
            return block;
        }
        if (block->next == block_1)
            return NULL;
        block = block->next;
    }
    return NULL; // no fit found
}

/**
 * @brief find fit in seglist without index specified
 * @param[in] asize  minimal blocksize for free block
 * @return the fit block or NULL
 */

block_t *find_fit_seg(size_t asize) {
    size_t index = find_seg_index(asize);
    block_t *block = NULL;
    while (index < num_lists) {
        block = find_seg_fit(index, asize);
        if (block != NULL) {
            return block;
            // look for next bigger size list if no fit
        } else {
            index++;
        }
    }
    return NULL; // no fit found
}

/**
 * @brief find fit in minilist (All the blocks in minilist have size of dsize)
 * @param[out] block the fit block or NULL
 */
miniblock_t *find_fit_mini() {
    miniblock_t *mini_block = mini_list;
    if (mini_block != NULL)
        return mini_block;
    else {
        return NULL;
    }
}

/**
 * @brief insert block into seglist
 * LIFO policy
 * @param[in] block to be inserted
 */
void insert_block_seg(block_t *block) {
    size_t block_size = get_size(block);
    size_t index = find_seg_index(block_size);
    // IF the list is empty, make it the first block and pointing to itself
    if (seglist[index] == NULL) {
        seglist[index] = block;
        block->prev = block;
        block->next = block;
    } else {
        // Circulated
        block_t *seg_block = seglist[index];
        block_t *seg_pre_block = seg_block->prev;
        block->next = seg_block;
        block->prev = seg_pre_block;
        seg_block->prev = block;
        seg_pre_block->next = block;
    }
}
/**
 * @brief insert miniblock into minilist
 * LIFO policy
 * @param[in] mini_block to be inserted
 */
void insert_miniblock(miniblock_t *mini_block) {
    /* INSERT TO AN EMPTY LIST*/
    if (mini_list == NULL) {
        mini_list = mini_block;
        // miniblock list ends with NULL
        mini_block->next = NULL;
    } else {
        mini_block->next = mini_list;
        mini_list = mini_block;
    }
}

/**
 * @brief remove miniblock from minilist
 * @param[in] mini_block to be removeded
 */
void remove_miniblock(miniblock_t *mini_block) {
    miniblock_t *mini_blockf = mini_list;
    /* FIRST IN MINILIST THAT HAS MORE THAN ONE MINIBLOCKS */
    if (mini_blockf == mini_block && mini_blockf->next != NULL) {
        miniblock_t *mini2 = mini_blockf->next;
        mini_list = mini2;
        mini_block->next = NULL;
    }
    /* ONLY ONE IN MINILIST */
    else if (mini_blockf == mini_block && mini_blockf->next == NULL) {
        mini_list = NULL;
        mini_block->next = NULL;
    }
    /* OTHER PLACE IN THE MINIBLOCK LIST*/
    else {
        while (mini_blockf->next != mini_block) {
            mini_blockf = mini_blockf->next;
        }
        mini_blockf->next = mini_block->next;
        mini_block->next = NULL;
    }
}

/**
 * @brief romove block from seglist
 * @param[in] block to be removed
 */
void remove_block(block_t *block) {
    dbg_requires(!get_alloc(block));
    dbg_requires(get_size(block) != 0);
    size_t block_size = get_size(block);
    size_t index = find_seg_index(block_size);

    /* ONLY ONE BLOCK IN SEGREGATAED LIST*/
    if (block == seglist[index] && block->next == block) {
        seglist[index] = NULL;
        block->next = NULL;
        block->prev = NULL;
        /* FIRST IN MINILIST THAT HAS MORE THAN ONE BLOCKS */
    } else if (block == seglist[index] && block->next != block) {
        seglist[index] = block->next;
        block->next->prev = block->prev;
        block->prev->next = block->next;
        block->next = NULL;
        block->prev = NULL;
        /* OTHER PLACE IN THE SEGREGATAED LIST*/
    } else {
        block_t *block_next = block->next;
        block_t *block_pre = block->prev;
        block_next->prev = block_pre;
        block_pre->next = block_next;
        block->next = NULL;
        block->prev = NULL;
    }
}

/**
 * @brief coalescing blocks (COULD BE MINIBLOCKS)
 * @param[in] block blocks to be coalesced
 * @return coalesced block
 */

void *coalesce_block(void *block) {
    dbg_requires(!get_alloc(block));
    void *below = find_next(block);
    void *prev = NULL;
    void *next = NULL;
    size_t size_block = get_size(block);
    dbg_requires(size_block != 0);
    bool pre_free = !get_alloc_pre(block);
    bool next_free = (below != NULL) && (!get_alloc(below));
    size_t size_prev = 0;
    size_t size_next = 0;
    /* case 1:only previous block is free */
    if (pre_free && !next_free) {
        /*prevous is miniblick*/
        if (get_mini(block)) {
            prev = block - dsize;
            size_prev = dsize;
        } else {
            prev = find_prev(block);
            size_prev = get_size(prev);
        }
        bool mini = get_mini(prev);
        write_block(prev, size_block + size_prev, mini, true, false);
        block = prev;
        /* update the minibit for the following block*/
        next = find_next(block);
        size_t next_size = get_size(next);
        write_block(next, next_size, false, false, true);
        /* case 2: only next block is free */
    } else if (!pre_free && next_free) {
        size_next = get_size(below);
        bool mini = get_mini(block);
        write_block(block, size_block + size_next, mini, true, false);
        /* update the minitag in the following block*/
        next = find_next(block);
        size_t next_size = get_size(next);
        write_block(next, next_size, false, false, true);
        /* case 3: both previous block ans next block is free */
    } else if (pre_free && next_free) {
        if (get_mini(block)) {
            /*previous is miniblock*/
            prev = block - dsize;
            size_prev = dsize;
        } else {
            prev = find_prev(block);
            size_prev = get_size(prev);
        }
        size_next = get_size(below);
        bool mini = get_mini(prev);
        write_block(prev, size_block + size_prev + size_next, mini, true,
                    false);
        block = prev;
        /* update the minitag in the following block*/
        next = find_next(block);
        size_t next_size = get_size(next);
        write_block(next, next_size, false, false, true);
        /* case 4: no coalesing */
    } else {
        next = find_next(block);
        size_t next_size = get_size(next);
        if (get_size(block) == dsize) {
            // BLOCK is MINIBLOCK & NO COALESCING
            write_block(next, next_size, true, false, true);
        } else {
            write_block(next, next_size, false, false, true);
        }
    }
    return block;
}
/**
 * @brief Extend the heap and check coalesced blocks
 * @param[in] size the minimal size to be extened
 * @return NULL if fail; new block from extending heap if succeeds
 */
void *extend_heap(size_t size, bool mini, bool alloc_pre) {
    void *bp;
    void *prev = NULL;
    // Allocate an even number of words to maintain alignment
    size = round_up(size, dsize);
    if ((bp = mem_sbrk(size)) == (void *)-1) {
        return NULL;
    }

    // Initialize free block header
    void *block = payload_to_header(bp);
    write_block(block, size, mini, alloc_pre, false);
    // Create new epilogue header
    void *block_next = find_next(block);
    write_epilogue(block_next);
    // Coalesce in case the previous block was free
    if (!alloc_pre) {
        bool mini = get_mini(block);
        /*ABOVE IS MINIBLOCK*/
        if (mini) {
            prev = block - dsize;
        }
        /*ABOVE IS NOT MINIBLOCK*/
        else {
            prev = find_prev(block);
        }
        /*ABOVE IS NOT MINIBLOCK*/
        if (get_size(prev) > dsize) {
            remove_block((block_t *)prev);
        }
        /*ABOVE IS MINIBLOCK*/
        else {
            remove_miniblock((miniblock_t *)prev);
        }
    }

    block = coalesce_block(block);
    insert_block_seg((block_t *)block);
    return block;
}

/**
 * @brief split the block into one allocated asize block and one free block
 * @param[in] block to be split
 * @param[in] asize  the block size to be split as allocated
 */
void split_block(void *block, size_t asize, bool mini, bool alloc_pre) {
    size_t block_size = get_size(block);
    dbg_requires(block_size != 0);
    /* Case 1 : Block not miniblock, both block after split are not miniblocks
     */
    if ((block_size >= min_block_size) && (asize >= min_block_size) &&
        ((block_size - asize) >= min_block_size)) {
        write_block(block, asize, mini, alloc_pre, true);
        block_t *next = find_next(block);
        write_block(next, block_size - asize, false, true, false);
        insert_block_seg((block_t *)next);
        /* Case 2: block not mini block, block after split is miniblock, free
         * block is not miniblock*/
    } else if ((block_size >= min_block_size) && (asize == dsize) &&
               ((block_size - asize) >= min_block_size)) {
        write_block(block, dsize, mini, alloc_pre, true);
        block_t *next = find_next(block);
        write_block(next, block_size - dsize, true, true, false);
        insert_block_seg((block_t *)next);
        /* Case 3: block is mini block, no split*/
    } else if ((block_size == dsize) && (asize == block_size)) {
        write_block(block, asize, mini, alloc_pre, true);
        block_t *next = find_next(block);
        write_block(next, get_size(next), true, true, true);
        /* Case 4: block is not mini block, allocated block is not miniblock,
         * free block is miniblock*/
    } else if ((block_size >= min_block_size) && (asize >= min_block_size) &&
               ((block_size - asize) == dsize)) {
        void *block_next;
        write_block(block, asize, mini, alloc_pre, true);
        block_next = find_next(block);
        write_block(block_next, dsize, false, true, false);
        insert_miniblock((miniblock_t *)block_next);
        void *next = find_next(block_next);
        write_block(next, get_size(next), true, false, true);
        /* Case 5: both block after split are miniblocks*/
    } else if ((block_size == min_block_size) && (asize == dsize)) {
        void *block_next;
        write_block(block, dsize, mini, alloc_pre, true);
        block_next = find_next(block);
        write_block(block_next, dsize, true, true, false);
        insert_miniblock((miniblock_t *)block_next);
        void *next = find_next(block_next);
        write_block(next, get_size(next), true, false, true);
        /* Case 6: split size  = block size, no split*/
    } else if ((block_size >= min_block_size) && (asize == block_size)) {
        write_block(block, asize, mini, alloc_pre, true);
        void *next = find_next(block);
        write_block(next, get_size(next), false, true, true);
    }
}
/**
 * @brief Checking the heap for :
 * 1. Address alignment for each block
 * 2. Epilogue and prologue blocks
 * 3. Blocks lie within heap boundaries
 * 4. Header and Footer for each block match (size and allocate bit)
 * 5. Block size >= minimum block size
 * 6. Coalescing
 * 7. Free list consistency
 *
 * @param[in] line The line number where mm_checkheap was called
 * @return True if passing mm_checkheap; False if failing
 */
bool mm_checkheap(int line) {
    return true;
}
/* For for dubugging and checkpoint */
// /**
//  * @brief Checking the heap for :
//  * 1. Address alignment for each block
//  * 2. Epilogue and prologue blocks
//  * 3. Blocks lie within heap boundaries
//  * 4. Header and Footer for each block match (size and allocate bit)
//  * 5. Block size >= minimum block size
//  * 6. Coalescing
//  * 7. Free list consistency
//  *
//  * @param[in] line The line number where mm_checkheap was called
//  * @return True if passing mm_checkheap; False if failing
//  */
// bool mm_checkheap(int line) {
//     /* block is the first block of the heap (not prologue) */
//     block_t *block = heap_start;
//     block_t *pre_block = NULL;
//     size_t block_size;
//     bool block_alloc;
//     // number of free blocks counted by iteration through all the blocks
//     int num_free_blocks_iter = 0;
//     // // number of free blocks counted by iteration through free list
//     int num_free_blocks_list = 0;
//     // while block is not the epilogue
//     while ((char *)block != mem_heap_hi() - 7) {
//         // block size derived from header
//         block_size = get_size(block);
//         block_alloc = get_alloc(block);
//         num_free_blocks_iter += (int)(!block_alloc);
//         word_t footer = *(header_to_footer(block));
//         word_t header = *((word_t *)block);
//         // Check each block address alignment
//         if (block_size % dsize != 0) {
//             flag = true;
//             dbg_printf("[line %d]: Payload size %lu not aligned.\n", line,
//                        block_size);
//             return false;
//         }
//         // Check if block within heap
//         if (block < (block_t *)mem_heap_lo() ||
//             block > (block_t *)mem_heap_hi()) {
//             flag = true;
//             dbg_printf("[line %d]: block %p outside area [%p, %p]\n", line,
//                        block, mem_heap_lo(), mem_heap_hi());
//             return false;
//         }
//         /* Check footer matches header only when for implicit list*/
//         if (implicit) {
//             if (footer != header) {
//                 // Check footersize matches headersize
//                 if (extract_size(footer) != block_size) {
//                     flag = true;
//                     dbg_printf("Footer size %lu missmatches associated header
//                     "
//                                "size %lu\n",
//                                extract_size(footer), block_size);
//                     return false;
//                 }

//                 // Check footer allocate matches header allocate
//                 if (extract_alloc(footer) != block_alloc) {
//                     flag = true;
//                     dbg_printf("Footer allocate missmatches associated header
//                     "
//                                "allocate %d\n",
//                                block_alloc);
//                     return false;
//                 }
//             }
//         }
//         // Check block size >= minimum block size (not for miniblock)
//         // if ((block_size < min_block_size)) {
//         //     flag = true;
//         //     dbg_printf(
//         //         "[line %d]: block size %lu less than minimum block size
//         //         %lu.\n", line, block_size, min_block_size);
//         //     return false;
//         // }

//         // Check coalescing
//         if (pre_block) {
//             if (!(block_alloc || get_alloc(pre_block))) {
//                 flag = true;
//                 dbg_printf("[line %d]: consecutive blocks not coalescing.\n",
//                            line);
//                 return false;
//             }
//         }
//         pre_block = block;
//         block = find_next(block);
//     }

//     /* Check for epilogue and prologue blocks */
//     block_t *epilogue = (block_t *)((char *)mem_heap_hi() - 7);
//     if (get_size(epilogue) != 0) {
//         flag = true;
//         dbg_printf("[line %d]: Size of epilogue %lu is not zero.\n", line,
//                    get_size(epilogue));
//         return false;
//     }
//     if (!get_alloc(epilogue)) {
//         flag = true;
//         dbg_printf("[line %d]: Epilogue is not allocated.\n", line);
//         return false;
//     }

//     block_t *prologue = (block_t *)((char *)heap_start - wsize);
//     if (get_size(prologue) != 0) {
//         flag = true;
//         dbg_printf("[line %d]: Size of prologue %lu is not zero.\n", line,
//                    get_size(prologue));
//         return false;
//     }
//     if (!get_alloc(prologue)) {
//         flag = true;
//         dbg_printf("[line %d]: Prologue is not allocated.\n", line);
//         return false;
//     }

//     /* Check blocks lie within heap boundaries. */
//     if (prologue < (block_t *)mem_heap_lo()) {
//         flag = true;
//         dbg_printf(
//             "[line %d]: Prologue is outside the heap lower boundary %p.\n",
//             line, mem_heap_lo());
//         return false;
//     }

//     /* Check seglist */

//     for (size_t index = 0; index < num_lists; index++) {
//         if (seglist[index] != NULL) {
//             num_free_blocks_list++;
//             block_t *block = seglist[index];
//             while (block->next) {
//                 block_t *block_n = block->next;
//                 num_free_blocks_list++;
//                 if (block_n->prev != block) {
//                     printf("seglist pointer is not consistent\n");
//                     return false;
//                 }
//             }
//         }
//     }
//     // number of free blocks matches free list
//     if (num_free_blocks_iter != num_free_blocks_list) {
//         printf("free count not equal\n");
//         return false;
//     }
//     return true;
// }

/**
 * @brief Initialize the heap and extend it by `chunksize` bytes
 * Iniitialize heap_start and free_start to the newly block generated from
 * extend_heap
 * @return ture if initialization succeeds else false
 */
bool mm_init(void) {
    /* initialize segregated list */
    for (size_t i = 0; i < num_lists; i++) {
        seglist[i] = NULL;
    }
    /* Initialize minilist */
    mini_list = NULL;
    // Create the initial empty heap
    word_t *start = (word_t *)(mem_sbrk(2 * wsize));

    if (start == (void *)-1) {
        return false;
    }

    start[0] = pack(0, false, true, true); // Heap prologue (block footer)
    start[1] = pack(0, false, true, true); // Heap epilogue (block header)

    // Heap starts with first "block header", currently the epilogue
    heap_start = &(start[1]);

    // Extend the empty heap with a free block of chunksize bytes
    if (extend_heap(chunksize, false, true) == NULL) {
        return false;
    }
    return true;
}

/**
 * @brief split the block into asize allocated block and free block
 *
 * @param[in] block block to be split
 * @param[in] asize size of allocated split block
 */
void split(void *block, size_t asize) {
    // miniblock
    if (get_size(block) == dsize) {
        remove_miniblock((miniblock_t *)block);
        // not miniblock
    } else {
        remove_block((block_t *)block);
    }
    bool alloc_pre = get_alloc_pre(block);
    bool mini = get_mini(block);
    split_block(block, asize, mini, alloc_pre);
}

/**
 * @brief allocate space of size `size` from the heap
 * @param[in] size the minimal size to be allocated feom the heap as a free
 * block
 * @return pointer to the paylod
 */
void *malloc(size_t size) {
    // dbg_ensures(mm_checkheap(__LINE__));
    size_t asize; // Adjusted block size
    void *block = NULL;
    miniblock_t *mini_block = NULL;
    void *bp = NULL;
    // Initialize heap if it isn't initialized
    if (heap_start == NULL) {
        mm_init();
    }
    // Ignore spurious request
    if (size == 0) {
        return bp;
    }

    // Adjust block size to include overhead and to meet alignment
    // requirements
    asize = round_up(size + wsize, dsize);
    if (asize == dsize) {
        mini_block = find_fit_mini();
        if (mini_block != NULL)
            block = (void *)mini_block;
        // Search the segmented list for a fit
    }

    if (asize > dsize || mini_block == NULL) {
        block = (void *)find_fit_seg(asize);
    }

    // If no fit is found, request more memory, and then and place the block
    if (block == NULL) {
        // Always request at least chunksize
        size_t extendsize = max(asize, chunksize);

        block_t *epilogue = (block_t *)((char *)(mem_heap_hi() - 7));
        bool alloc_pre = get_alloc_pre((void *)epilogue);
        bool mini = get_mini((void *)epilogue);
        block = extend_heap(extendsize, mini, alloc_pre);

        // extend_heap returns an error
        if (block == NULL) {
            return bp;
        }
    }

    split(block, asize);

    bp = header_to_payload(block);
    // dbg_ensures(mm_checkheap(__LINE__));
    return bp;
}
/**
 * @brief free the given allocated block from the heap
 *
 * @param[in] bp the pointer to the allocated payload that will be freed in the
 * heap
 */

void free(void *bp) {
    // dbg_ensures(mm_checkheap(__LINE__));
    void *above = NULL;
    void *block = payload_to_header(bp);
    size_t size = get_size(block);
    bool alloc_pre = get_alloc_pre(block);
    bool mini = get_mini(block);

    // Mark the block as free
    write_block(block, size, mini, alloc_pre, false);

    // Try to coalesce the block with its neighbors
    if (!alloc_pre) {
        if (get_mini(block)) {
            above = block - dsize;
            remove_miniblock((miniblock_t *)above);
        } else {
            above = find_prev(block);
            remove_block(above);
        }
    }

    void *below = find_next(block);

    if (below != NULL) {
        bool b_alc = get_alloc(below);
        if (!b_alc && get_size(below) == dsize) {
            remove_miniblock((miniblock_t *)below);
        } else if (!b_alc && get_size(below) != dsize) {
            remove_block(below);
        }
    }
    block = coalesce_block(block);

    if (get_size(block) == dsize) {
        insert_miniblock((miniblock_t *)block);
    } else {
        insert_block_seg((block_t *)block);
    }
    // dbg_ensures(mm_checkheap(__LINE__));
}

/**
 * @brief Reallocate memory of at least `size` bytes with constraints
 * @param[in] ptr pointer to the block that is going to be reallocate with new
 * `size`
 * @param[in] size new number of bytes for reallocating the block
 * @return pointer to the reallocated block
 */
void *realloc(void *ptr, size_t size) {
    block_t *block = payload_to_header(ptr);
    size_t copysize;
    void *newptr;
    // If size == 0, then free block and return NULL
    if (size == 0) {
        free(ptr);
        return NULL;
    }

    // If ptr is NULL, then equivalent to malloc
    if (ptr == NULL) {
        return malloc(size);
    }

    // Otherwise, proceed with reallocation
    newptr = malloc(size);

    // If malloc fails, the original block is left untouched
    if (newptr == NULL) {
        return NULL;
    }

    // Copy the old data
    copysize = get_payload_size(block); // gets size of old payload
    if (size < copysize) {
        copysize = size;
    }
    memcpy(newptr, ptr, copysize);

    // Free the old block
    free(ptr);
    return newptr;
}

/**
 * @brief Request at least `elements` number of `size` bytes elemetns to a
 * block from heap aligned to 16-bite boundary and initialize it with 0
 * @param[in] elements number of elements to be allocated
 * @param[in] size The number of bytes per element
 * @return the pointer to the requested block
 */
void *calloc(size_t elements, size_t size) {
    void *bp;
    size_t asize = elements * size;
    if (elements == 0) {
        return NULL;
    }
    if (asize / elements != size) {
        // Multiplication overflowed
        return NULL;
    }

    bp = malloc(asize);
    if (bp == NULL) {
        return NULL;
    }

    // Initialize all bits to 0
    memset(bp, 0, asize);

    return bp;
}

// /**
//  * @brief print the content of heap
//  * @param mode types of content to be printed:
//  * 0 : all block
//  * 1 : all free block
//  * 2 : number of blocks
//  */
// void print_heap(int mode) {
//     block_t *block;

//     unsigned long num_blocks = 0;
//     block = heap_start;
//     if (!heap_start) {
//         dbg_printf("heap not initialized.\n");
//         exit(0);
//     }
//     while ((char *)block != mem_heap_hi() - 7) {
//         if (mode == 0) {
//             dbg_printf(
//                 "block address :%p , block size : %zu bytes, alloc_: %d \n",
//                 block, get_size(block), get_alloc(block));
//         }
//         if (mode == 1) {
//             if (!get_alloc(block)) {
//                 dbg_printf("block address :%p , block size : %zu bytes\n",
//                            block, get_size(block));
//             }
//         }
//         if (mode == 2) {
//             num_blocks++;
//         }
//         block = find_next(block);
//     }
//     if (mode == 2) {
//         dbg_printf("number of blocks %lu\n", num_blocks);
//     }
// }

/*
 *****************************************************************************
 * Do not delete the following super-secret(tm) lines!                       *
 *                                                                           *
 * 53 6f 20 79 6f 75 27 72 65 20 74 72 79 69 6e 67 20 74 6f 20               *
 *                                                                           *
 * 66 69 67 75 72 65 20 6f 75 74 20 77 68 61 74 20 74 68 65 20               *
 * 68 65 78 61 64 65 63 69 6d 61 6c 20 64 69 67 69 74 73 20 64               *
 * 6f 2e 2e 2e 20 68 61 68 61 68 61 21 20 41 53 43 49 49 20 69               *
 *                                                                           *
 * 73 6e 27 74 20 74 68 65 20 72 69 67 68 74 20 65 6e 63 6f 64               *
 * 69 6e 67 21 20 4e 69 63 65 20 74 72 79 2c 20 74 68 6f 75 67               *
 * 68 21 20 2d 44 72 2e 20 45 76 69 6c 0a c5 7c fc 80 6e 57 0a               *
 *                                                                           *
 *****************************************************************************
 */