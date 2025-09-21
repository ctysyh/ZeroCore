/*
*/
#pragma once

#include <stdatomic.h>
#include "zerocore_internal.h"
#include "segment.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct zc_stats {
    uint64_t total_bytes;
    uint64_t used_bytes;
    uint64_t free_block_count;
    uint64_t using_block_count;
    uint64_t clean_ops;
    uint64_t merge_ops;
    uint64_t max_offset;          // 当前最大偏移量
    uint64_t backpressure_events;
    uint64_t heartbeat_missed;    // 心跳超时次数
    uint64_t reserved[8];
} zc_stats_t;

typedef struct zc_registry {
    //zc_writer_registry_t   writers[ZC_MAX_WRITERS];
    //zc_reader_registry_t   readers[ZC_MAX_READERS_TOTAL];
    //zc_cleaner_context_t   cleaners[ZC_MAX_CLEANERS];
    atomic_flag            lock; // 仅用于注册/注销临界区
} zc_registry_t;

typedef struct zc_memory_pool {
    char* name;                   // 名称
    zc_segment_t* segments;       // 池内存段
    atomic_size_t segment_count;  // 内存段数量
    zc_stats_t stats;             // 全局统计
    zc_registry_t registry;       // 注册表

    // === 内部线程资源 ===

} zc_memory_pool_t;

#ifdef __cplusplus
}
#endif