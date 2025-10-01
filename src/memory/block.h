/*
*/
#pragma once

#include "zerocore_internal.h"
#include "page.h"

#ifndef BLOCK_H
#define BLOCK_H

#ifdef __cplusplus
extern "C" {
#endif

#ifndef ZC_BLOCK_HEADER_SIZE
#define ZC_BLOCK_HEADER_SIZE 128
#endif

#ifndef ZC_BLOCK_MAX_CACHED_PAGES
#define ZC_BLOCK_MAX_CACHED_PAGES 7
#endif

typedef struct zc_block_header {
    _Atomic uint16_t  state;             // FREE=0, USING=1, CLEAN=2
    uint16_t          reserved_flags;    // 未来扩展位
    zc_writer_id_t    writer_id;         // 写入者 ID
    zc_time_t         timestamp;         // 写入时间戳
    uint64_t          cover_page_count;  // 块跨越的页数量
    uint64_t          lut_offset;        // DTTA 查找表偏移量(从 header 首地址开始计算)

    // === 块页缓存 ===
    uint8_t    lut_disabled;
    uint8_t    reserved_flags[7];
    zc_page_t* page_cache[ZC_BLOCK_MAX_CACHED_PAGES]; // 块页缓存，用于快速访问前7页；如果块页数较多，在第8页补充一个小的mid_metadata_cache，在其中记录接下来的7个页，依此类推

    // === 并行引用位图===
    bool      writer_ref[ZC_MAX_WRITERS];         // 写入者实时引用
    bool      reader_ref[ZC_MAX_READERS_PER];     // 读取者实时引用
    bool      reader_visited[ZC_MAX_READERS_PER]; // 读取者访问历史
} zc_block_header_t;

typedef enum zc_block_state {
    ZC_BLOCK_STATE_FREE   = 0,
    ZC_BLOCK_STATE_USING  = 1,
    ZC_BLOCK_STATE_CLEAN  = 2,
} zc_block_state_t;

zc_internal_result_t zc_acquire_block_for_writing(
    zc_block_header_t* block,
    uint64_t size,
    zc_writer_id_t writer_id
);

zc_internal_result_t zc_acquire_block_for_reading(
    zc_block_header_t* block,
    zc_reader_id_t reader_id
);

zc_internal_result_t zc_acquire_block_for_cleaning(
    zc_block_header_t* block
);

zc_internal_result_t zc_block_create(
    void* block_start_ptr,
    uint64_t userdate_size,
    uint64_t page_count
);

zc_internal_result_t zc_block_delete(
    zc_block_header_t* block,
    uint64_t* release_page_count,
    zc_writer_id_t* writer_id,
    zc_time_t* timestamp
);

// 已检查，未测试
inline void* zc_block_offset_to_ptr(
    zc_block_header_t* block,
    uint64_t offset
);

inline uint64_t zc_block_ptr_to_offset(
    zc_block_header_t* block,
    void* ptr
);

#ifdef __cplusplus
}
#endif

#endif /* BLOCK_H */