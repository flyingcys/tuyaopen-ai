/**
 * @file tkl_memory.c
 * @brief the default weak implements of tuya hal memory, this implement only used when OS=linux
 * @version 0.1
 * @date 2020-05-15
 * 
 * @copyright Copyright 2020-2021 Tuya Inc. All Rights Reserved.
 * 
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include "tkl_memory.h"
#include "tuya_mem_heap.h"

#define MAX_HEAP_SIZE (512*10*1024)

static pthread_mutex_t s_heap_mutex = PTHREAD_MUTEX_INITIALIZER;
static HEAP_HANDLE s_heap_handle = NULL;

static void __heap_lock(void)
{
    pthread_mutex_lock(&s_heap_mutex);
}

static void __heap_unlock(void)
{
    pthread_mutex_unlock(&s_heap_mutex);
}

static void __heap_init(void)
{
    heap_context_t ctx = {0};
    ctx.dbg_output = (void (*)(char* , ...))printf;
    ctx.enter_critical = __heap_lock;
    ctx.exit_critical = __heap_unlock;

    char* buf = malloc(MAX_HEAP_SIZE);
    tuya_mem_heap_init(&ctx);
    tuya_mem_heap_create(buf, MAX_HEAP_SIZE, &s_heap_handle);
}

/**
* @brief Alloc memory of system
*
* @param[in] size: memory size
*
* @note This API is used to alloc memory of system.
*
* @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
*/
void* tkl_system_malloc(const SIZE_T size)
{
    // if(!s_heap_handle) {
    //     __heap_init();
    // }

    // return tuya_mem_heap_malloc(s_heap_handle, size);
    void * p = malloc(size);
    // printf("=================p :%p\n", p);
    return p;
}

/**
* @brief Free memory of system
*
* @param[in] ptr: memory point
*
* @note This API is used to free memory of system.
*
* @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
*/
void tkl_system_free(void* ptr)
{
    // if(ptr) {
    //     tuya_mem_heap_free(s_heap_handle, ptr);
    // }

    free(ptr);
}

/**
 * @brief Allocate and clear the memory
 * 
 * @param[in]       nitems      the numbers of memory block
 * @param[in]       size        the size of the memory block
 */
void *tkl_system_calloc(size_t nitems, size_t size)
{
    // return tuya_mem_heap_calloc(s_heap_handle, nitems * size);

    return calloc(nitems, size);
}

/**
 * @brief Re-allocate the memory
 * 
 * @param[in]       nitems      source memory address
 * @param[in]       size        the size after re-allocate
 */
void *tkl_system_realloc(void* ptr, size_t size)
{
    // return tuya_mem_heap_realloc(s_heap_handle, ptr, size);

    return realloc(ptr, size);
}

/**
* @brief Get free heap size
*
* @param void
*
* @note This API is used for getting free heap size.
*
* @return size of free heap
*/
int tkl_system_get_free_heap_size(void)
{
    // return tuya_mem_heap_available(s_heap_handle);

    return MAX_HEAP_SIZE;
}

/**
* @brief set memory
*
* @param[in] size: memory size
*
* @note This API is used to alloc memory of system.
*
* @return the memory address malloced
*/
TUYA_WEAK_ATTRIBUTE void *tkl_system_memset(void* src, int ch, const SIZE_T n)
{
    return memset(src, ch, n);
}

/**
* @brief Alloc memory of system
*
* @param[in] size: memory size
*
* @note This API is used to alloc memory of system.
*
* @return the memory address malloced
*/
TUYA_WEAK_ATTRIBUTE void *tkl_system_memcpy(void* src, const void* dst, const SIZE_T n)
{
    return memcpy(src, dst, n);
}