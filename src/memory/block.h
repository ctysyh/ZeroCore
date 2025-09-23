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

typedef struct zc_block_header {
    _Atomic uint16_t  state;             // FREE=0, USING=1, CLEAN=2
    uint16_t          reserved_flags;    // 未来扩展位
    zc_writer_id_t    writer_id;         // 写入者 ID
    zc_time_t         timestamp;         // 写入时间戳
    uint64_t          cover_page_count;  // 块跨越的页数量
    uint64_t          lut_offset;        // DTTA 查找表偏移量(从 header 首地址开始计算)

    // === 并行引用位图===
    bool      writer_ref[ZC_MAX_WRITERS];         // 写入者实时引用
    bool      reader_ref[ZC_MAX_READERS_PER];     // 读取者实时引用
    bool      reader_visited[ZC_MAX_READERS_PER]; // 读取者访问历史
} zc_block_header_t;
#define ZC_BLOCK_HEADER_SIZE 128

typedef enum zc_block_state {
    ZC_BLOCK_STATE_FREE   = 0,
    ZC_BLOCK_STATE_USING  = 1,
    ZC_BLOCK_STATE_CLEAN  = 2,
} zc_block_state_t;

zc_internal_result_t zc_acquire_block_for_writing(
    zc_block_header_t* block,
    size_t size,
    zc_writer_id_t writer_id
);

zc_internal_result_t zc_acquire_block_for_reading(
    zc_block_header_t* block,
    zc_reader_id_t reader_id
);

zc_internal_result_t zc_acquire_block_for_cleaning(
    zc_block_header_t* block
);

zc_internal_result_t zc_block_create_new_header(
    void* block_start_ptr,
    uint64_t page_count
);

zc_internal_result_t zc_block_delete_header(
    zc_block_header_t* block,
    uint64_t* release_page_count,
    zc_writer_id_t* writer_id,
    zc_time_t* timestamp
);

#ifdef __cplusplus
}
#endif

#endif /* BLOCK_H */