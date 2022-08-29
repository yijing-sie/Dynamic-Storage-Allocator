# Dynamic-Storage-Allocator

Intro to Computer Systems assignment. This assignment was built and (supposed to) run in Linux:


The goal is to build a Dynamic-Storage-Allocator that achieves high **space utilization** and **throughput** performance

### [mm.c](mm.c) consists of my own version of malloc, free, realloc, and calloc functions to build such dynamic storage allocator
*  It supports a full 64-bit address space
*  It employs mini-blockas and segregated list for speed and memory efficiency 
*  I implemented the following functions in [mm.c](mm.c):
```
1. bool mm_init(void) : performs any necessary initializations, such as allocating the initial heap area
2. void *malloc(size_t size)
3. void free(void *ptr)
4. void *realloc(void *ptr, size_t size)
5. void *calloc(size_t nmemb, size_t size); bool mm_checkheap(int);
6. bool mm_checkheap(int line): scans the heap and checks it for possible errors.
7. void print_heap(int mode): prints the content of heap in different modes
```
### Evaluation
Two metrics are used to evaluate performance: utilization and throughput:


* A single performance index `Perf Index`, with 0 ≤ P ≤ 100, computed as a weighted sum of the space utilization and throughput

| Utilization | Throughput |
--- | --- |
60% | 40% | 

* Space Utilization: The peak ratio between the aggregate amount of memory used by the driver (i.e., allocated via `malloc` but not yet freed via `free`) and the size of the heap used by my allocator. The utilization score is calculated as the average utilization across all traces.

* Throughput: The average number of operations completed per second, expressed in kilo-operations per second or KOPS. A trace that takes T seconds to perform n operations will have a throughput of n/(1000 · T) KOPS. The throughput is calculated as the average throughput across all traces.

Here's the report for my allocator:


![image](https://user-images.githubusercontent.com/84282744/187279131-e2e33fb9-798c-4bfc-b9f2-8f4a96972724.png)



