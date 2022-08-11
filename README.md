# Dynamic-Storage-Allocator

Intro to Computer Systems assignment :



*   mm.c consists of my own version of malloc, free, realloc, and calloc functions to build a dynamic storage allocator that is correct, efficient, and fast
*  It  supports a full 64-bit address space
*  I implemented the following functions in mm.c:

1. bool mm_init(void) : performs any necessary initializations, such as allocating the initial heap area
2. void *malloc(size_t size)
3. void free(void *ptr)
4. void *realloc(void *ptr, size_t size)
5. void *calloc(size_t nmemb, size_t size); bool mm_checkheap(int);
6. bool mm_checkheap(int line): scans the heap and checks it for possible errors.


.



