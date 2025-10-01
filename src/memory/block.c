#include "block.h"
#include "page.h"
#include "dtta.h"
#include <string.h>

/**
 * 
 * 
 */
zc_internal_result_t zc_acquire_block_for_writing(zc_block_header_t* block,
    uint64_t acquire_size, zc_writer_id_t writer_id)
{
    // 假设传入的参数都是有效的

    if (block->state != ZC_BLOCK_STATE_FREE) return ZC_INTERNAL_BLOCK_UNEXPECTED;

    uint64_t need_page_count = (ZC_BLOCK_HEADER_SIZE + acquire_size + (acquire_size / 10)) / ZC_PAGE_DATA_SIZE + 1;
    if (need_page_count > block->cover_page_count) return ZC_INTERNAL_BLOCK_UNEXPECTED;

    int32_t flag;
    uint32_t i;
    for (i = 0; i < ZC_MAX_WRITERS; i++)
    {
        if (block->writer_ref[i]) return ZC_INTERNAL_BLOCK_UNRELEASED;
    }
    for (i = 0; i < ZC_MAX_READERS_PER; i++)
    {
        if (block->reader_ref[i]) return ZC_INTERNAL_BLOCK_UNRELEASED;
    }

    block->writer_ref[writer_id] = true;
    block->lut_offset = acquire_size + ZC_BLOCK_HEADER_SIZE;

    // 暂时忽略与清理者的冲突
    // CAS block->state = ZC_BLOCK_STATE_FREE

    for (i = 0; i < writer_id; i++)
    {
        if (block->writer_ref[i] == true)
        {
            block->writer_ref[writer_id] = false;
            return ZC_INTERNAL_BLOCK_WRITER_CONFLICT;
        }
    }

    block->writer_id = writer_id;

    // 省略工作空间的更新

    if (need_page_count + 1 < block->cover_page_count)
    {
        void* current_page = (char*)block - ZC_PAGE_HEADER_SIZE;
        zc_page_t* next_header_page = (zc_page_t*)current_page + need_page_count;

        uint64_t new_block_page_count = block->cover_page_count - need_page_count;
        flag = zc_block_create(next_header_page->data, new_block_page_count * (9 * ZC_PAGE_DATA_SIZE / 10), new_block_page_count);
        if (unlikely(flag != ZC_INTERNAL_OK))
        {
            // 不需要失败, 但是需要报告, 报告逻辑暂时忽略
        }
        else
        {
            block->lut_offset = ZC_BLOCK_HEADER_SIZE + acquire_size;
            block->cover_page_count = need_page_count;
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

    if (block->state != ZC_BLOCK_STATE_USING || block->writer_id != (reader_id >> 32)) return ZC_INTERNAL_BLOCK_UNEXPECTED;

    if (block->reader_visited[(uint32_t)reader_id]) return ZC_INTERNAL_BLOCK_UNEXPECTED;

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

    if (block->state != ZC_BLOCK_STATE_USING) return ZC_INTERNAL_BLOCK_UNEXPECTED;

    uint32_t i;
    for (i = 0; i < ZC_MAX_WRITERS; i++)
    {
        if (block->writer_ref[i]) return ZC_INTERNAL_BLOCK_UNRELEASED;
    }
    for (i = 0; i < ZC_MAX_READERS_PER; i++)
    {
        if (block->reader_ref[i]) return ZC_INTERNAL_BLOCK_UNRELEASED;
    }
    // 省略检查读取者访问历史, 因为需要额外数据

    block->state = ZC_BLOCK_STATE_CLEAN;

    for (i = 0; i < ZC_MAX_READERS_PER; i++) block->reader_visited[i] = false;

    return ZC_INTERNAL_OK;
}

/**
 * 在释放工作空间中的旧缓存时调用
 * 
 */
inline zc_internal_result_t zc_release_block_from_writing(zc_block_header_t* block,
    zc_writer_id_t writer_id)
{
    block->writer_ref[writer_id] = false;
}

/**
 * 在释放工作空间中的旧缓存时调用
 * 
 */
inline zc_internal_result_t zc_release_block_from_reading(zc_block_header_t* block,
    zc_reader_id_t reader_id)
{
    block->reader_ref[reader_id] = false;
}

/**
 * 
 */
zc_internal_result_t zc_block_create(void* block_start_ptr,
    uint64_t userdate_size, uint64_t page_count)
{
    // 假设传入的参数都是有效的

    void* _start_page = block_start_ptr - ZC_PAGE_HEADER_SIZE;
    zc_page_t* start_page = (zc_page_t*)_start_page;
    zc_block_header_t* block = block_start_ptr;

    // 后续应改为 CAS
    uint64_t i;
    for (i = 0; i < page_count; i++) (start_page + i)->header.state = ZC_PAGE_STATE_LOCK;

    block->state = ZC_BLOCK_STATE_FREE;
    block->cover_page_count = page_count;
    block->lut_offset = ZC_BLOCK_HEADER_SIZE + userdate_size;

    memset(block->writer_ref, 0, ZC_MAX_WRITERS + ZC_MAX_READERS_PER * 2);

    zc_dtt_lut_header_t* lut_header = (zc_dtt_lut_header_t*)(block->lut_offset);
    lut_header->entry_count = 0;
    lut_header->lut_first_entry_offset = zc_block_ptr_to_offset(block, lut_header + 1);

    // 后续应改为 CAS
    start_page->header.state = ZC_PAGE_STATE_AS_HEAD;
    for (i = 1; i < page_count; i++) (start_page + i)->header.state = ZC_PAGE_STATE_AS_MID;

    return ZC_INTERNAL_OK;
}

/**
 * 
 */
zc_internal_result_t zc_block_delete(zc_block_header_t* block,
    uint64_t* release_page_count, zc_writer_id_t* writer_id, zc_time_t* timestamp)
{
    // 假设传入的参数都是有效的

    void* _start_page = (char*)block - ZC_PAGE_HEADER_SIZE;
    zc_page_t* start_page = (zc_page_t*)_start_page;

    // 后续应改为 CAS
    uint64_t i;
    for (i = 0; i < block->cover_page_count; i++) (start_page + i)->header.state = ZC_PAGE_STATE_LOCK;

    *release_page_count = block->cover_page_count;
    *writer_id = block->writer_id;
    *timestamp = block->timestamp;

    // 后续应改为 CAS
    for (i = 0; i < *release_page_count; i++) (start_page + i)->header.state = ZC_PAGE_STATE_IDLE;

    return ZC_INTERNAL_OK;
}

// 已检查，未测试
inline void* zc_block_offset_to_ptr(zc_block_header_t* block, uint64_t offset)
{
    if (unlikely(offset >= block->cover_page_count * ZC_PAGE_DATA_SIZE))
    {
        return NULL; // out of block
    }

    uint64_t page_idx = offset / ZC_PAGE_DATA_SIZE;
    zc_page_t* page;

    if (likely(page_idx < ZC_BLOCK_MAX_CACHED_PAGES))
    {
        page = block->page_cache[page_idx];
    }
    else
    {
        page = block->page_cache[ZC_BLOCK_MAX_CACHED_PAGES - 1];
        for (uint64_t i = ZC_BLOCK_MAX_CACHED_PAGES - 1; i < page_idx && page; i++)
        {
            page = page->tail.next_page_addr;
        }
    }

    if (!page) return NULL;
    uint64_t in_page = offset % ZC_PAGE_DATA_SIZE;
    return (char*)page + ZC_PAGE_HEADER_SIZE + in_page;
}