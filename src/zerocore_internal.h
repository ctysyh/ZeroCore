/*
*/

#ifndef ZEROCORE_INTERNAL_H
#define ZEROCORE_INTERNAL_H

#define ZC_VERSION_MAJOR 1
#define ZC_VERSION_MINOR 4

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef ZC_CACHE_LINE_SIZE
#define ZC_CACHE_LINE_SIZE 512
#endif

#ifndef ZC_MAX_WRITERS
#define ZC_MAX_WRITERS 32
#endif

#ifndef ZC_MAX_READERS_PER
#define ZC_MAX_READERS_PER 32
#endif

#ifndef ZC_MAX_READERS_TOTAL
#define ZC_MAX_READERS_TOTAL 1024
#endif

#ifndef ZC_MAX_CLEANERS
#define ZC_MAX_CLEANERS 32
#endif

// 添加likely和unlikely宏定义
#ifndef likely
#define likely(x) __builtin_expect(!!(x), 1)
#endif

#ifndef unlikely
#define unlikely(x) __builtin_expect(!!(x), 0)
#endif

typedef uint32_t zc_writer_id_t;
typedef uint64_t zc_reader_id_t;

typedef enum zc_internal_result {
    ZC_INTERNAL_OK             = 0,
    ZC_INTERNAL_GENERAL_ERROR  = 1,

    ZC_INTERNAL_PARAM_ERROR    = 10,
    ZC_INTERNAL_PARAM_PTRNULL  = 11,

    ZC_INTERNAL_RUN_ERROR      = 20,
    ZC_INTERNAL_RUN_PTRNULL    = 21,
} zc_internal_result_t;

#ifdef __cplusplus
}
#endif

#endif /* ZEROCORE_INTERNAL_H */