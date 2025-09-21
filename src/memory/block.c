#include "block.h"
#include "page.h"
#include <string.h>

/**
 * 
 * 
 */
zc_internal_result_t zc_acquire_block_for_writing(zc_block_header_t* block,
    size_t size, zc_writer_id_t writer_id)
{
    // 假设传入的参数都是有效的

    if (block->state != ZC_BLOCK_STATE_FREE || block->lut_offset < size + ZC_BLOCK_HEADER_SIZE) return ZC_INTERNAL_RUN_BLOCK_UNEXPECTED;

    int32_t flag;
    uint32_t i;
    for (i = 0; i < ZC_MAX_WRITERS; i++) {
        if (block->writer_ref[i]) return ZC_INTERNAL_RUN_BLOCK_UNRELEASED;
    }
    for (i = 0; i < ZC_MAX_READERS_PER; i++) {
        if (block->reader_ref[i]) return ZC_INTERNAL_RUN_BLOCK_UNRELEASED;
    }

    block->writer_ref[writer_id] = true;

    // 暂时忽略与清理者的冲突

    for (i = 0; i < writer_id; i++) {
        if (block->writer_ref[i] == true)
        {
            block->writer_ref[writer_id] = false;
            return ZC_INTERNAL_RUN_BLOCK_WRITER_CONFLICT;
        }
    }

    // 省略工作空间的更新

    if (block->lut_offset - size - (size / 10) - ZC_BLOCK_HEADER_SIZE > ZC_PAGE_DATA_SIZE * 3)
    {
        // new_header_ptr 应该等于当前 block 占用的最后一个 page 的下一个 page.data
        // 当前 block 所需的长度是 ZC_BLOCK_HEADER_SIZE + size + (size / 10)
        // 因此当前 block 所需占用的块数量应该是 (ZC_BLOCK_HEADER_SIZE + size + (size / 10)) / ZC_PAGE_DATA_SIZE + 1
        uint64_t current_block_page_count = (ZC_BLOCK_HEADER_SIZE + size + (size / 10)) / ZC_PAGE_DATA_SIZE + 1;
        void* current_page = (void*)block - ZC_PAGE_HEADER_SIZE;
        zc_page_t* next_header_page = (zc_page_t*)current_page + current_block_page_count;

        // 而原本当前 block 占用的块总数是 block->lut_offset / ZC_PAGE_DATA_SIZE + 1
        uint64_t origin_block_page_count = block->lut_offset / ZC_PAGE_DATA_SIZE + 1;

        // 从而新的 block 可用的页数是 origin_block_page_count - current_block_page_count
        uint64_t new_block_page_count = origin_block_page_count - current_block_page_count;

        flag = zc_block_create_new_header((zc_block_header_t*)next_header_page->data, next_header_page, new_block_page_count);
        if (unlikely(flag != ZC_INTERNAL_OK))
        {
            // 不需要失败, 但是需要报告, 报告逻辑暂时忽略
        }
    }

    // 省略时间戳

    return ZC_INTERNAL_OK;
}

/**
 * 
 * 
 */
zc_internal_result_t zc_acquire_block_for_reading(zc_block_header_t* block,
    zc_reader_id_t reader_id)
{
    // 假设传入的参数都是有效的

    if (block->state != ZC_BLOCK_STATE_USING || block->writer_id != (reader_id >> 32)) return ZC_INTERNAL_RUN_BLOCK_UNEXPECTED;

    if (block->reader_visited[(uint32_t)reader_id]) return ZC_INTERNAL_RUN_BLOCK_UNEXPECTED;

    block->reader_visited[(uint32_t)reader_id] = true;
    block->reader_ref[(uint32_t)reader_id] = true;

    // 省略工作空间的更新

    return ZC_INTERNAL_OK;
}

/**
 * 
 * 
 */
zc_internal_result_t zc_acquire_block_for_cleaning(zc_block_header_t* block)
{
    // 假设传入的参数都是有效的

    if (block->state != ZC_BLOCK_STATE_USING) return ZC_INTERNAL_RUN_BLOCK_UNEXPECTED;

    uint32_t i;
    for (i = 0; i < ZC_MAX_WRITERS; i++) {
        if (block->writer_ref[i]) return ZC_INTERNAL_RUN_BLOCK_UNRELEASED;
    }
    for (i = 0; i < ZC_MAX_READERS_PER; i++) {
        if (block->reader_ref[i]) return ZC_INTERNAL_RUN_BLOCK_UNRELEASED;
    }
    // 省略检查读取者访问历史, 因为需要额外数据

    block->state = ZC_BLOCK_STATE_CLEAN;
    for (i = 0; i < ZC_MAX_READERS_PER; i++) block->reader_visited[i] = false;

    return ZC_INTERNAL_OK;
}

/**
 * 
 */
zc_internal_result_t zc_block_create_new_header(zc_block_header_t* block, zc_page_t* start_page, uint64_t page_count)
{
    // 假设传入的参数都是有效的

    // 后续应改为 CAS
    uint64_t i;
    for (i = 0; i < page_count; i++) (start_page + i)->header.state = ZC_PAGE_STATE_LOCK;

    block->state = ZC_BLOCK_STATE_FREE;
    block->lut_offset = page_count * ZC_PAGE_DATA_SIZE;
    block->lut_entry_count = 0;

    memset(block->writer_ref, 0, ZC_MAX_WRITERS + ZC_MAX_READERS_PER * 2);

    // 后续应改为 CAS
    start_page->header.state = ZC_PAGE_STATE_AS_HEAD;
    for (i = 1; i < page_count; i++) (start_page + i)->header.state = ZC_PAGE_STATE_AS_MID;

    return ZC_INTERNAL_OK;
}