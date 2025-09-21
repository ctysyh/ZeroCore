#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include "../src/memory/block.h"
#include "../src/memory/page.h"
#include "../src/memory/segment.h"

// 辅助函数，用于创建测试用的内存页面
zc_page_t* create_test_pages(int page_count) {
    zc_page_t* pages = malloc(sizeof(zc_page_t) * page_count);
    for (int i = 0; i < page_count; i++) {
        pages[i].header.state = ZC_PAGE_STATE_IDLE;
        pages[i].header.line_seq = i;
        pages[i].header.prev_page_addr = (i > 0) ? (uint64_t)&pages[i-1] : 0;
        pages[i].tail.next_page_addr = (i < page_count - 1) ? (uint64_t)&pages[i+1] : 0;
    }
    return pages;
}

void test_zc_block_create_new_header() {
    printf("Testing zc_block_create_new_header...\n");

    // 创建测试用的页面
    size_t page_count = 5;
    zc_page_t* pages = create_test_pages(page_count);
    
    // 测试创建新的块头
    zc_block_header_t* block = (zc_block_header_t*)pages[0].data;
    zc_internal_result_t result = zc_block_create_new_header(block, pages, page_count);
    
    // 验证结果
    assert(result == ZC_INTERNAL_OK);
    assert(block->state == ZC_BLOCK_STATE_FREE);
    assert(block->lut_offset == page_count * ZC_PAGE_DATA_SIZE);
    assert(block->lut_entry_count == 0);
    
    // 验证页面状态
    assert(pages[0].header.state == ZC_PAGE_STATE_AS_HEAD);
    for (size_t i = 1; i < page_count; i++) {
        assert(pages[i].header.state == ZC_PAGE_STATE_AS_MID);
    }
    
    // 验证引用位图被清零
    for (int i = 0; i < ZC_MAX_WRITERS; i++) {
        assert(block->writer_ref[i] == false);
    }
    for (int i = 0; i < ZC_MAX_READERS_PER; i++) {
        assert(block->reader_ref[i] == false);
        assert(block->reader_visited[i] == false);
    }
    
    printf("  Passed zc_block_create_new_header test\n");
    free(pages);
}

void test_zc_acquire_block_for_writing() {
    printf("Testing zc_acquire_block_for_writing...\n");

    // 创建测试用的页面和块
    int page_count = 5;
    zc_page_t* pages = create_test_pages(page_count);
    zc_block_header_t* block = (zc_block_header_t*)pages[0].data;
    
    // 首先初始化块
    zc_block_create_new_header(block, pages, page_count);
    
    // 测试正常获取写入权限
    zc_writer_id_t writer_id = 1;
    size_t size = 1024; // 请求1KB空间
    zc_internal_result_t result = zc_acquire_block_for_writing(block, size, writer_id);
    
    // 验证结果
    assert(result == ZC_INTERNAL_OK);
    assert(block->writer_ref[writer_id] == true);
    assert(block->state == ZC_BLOCK_STATE_FREE); // 状态应该保持FREE
    
    printf("  Passed normal zc_acquire_block_for_writing test\n");
    
    // 测试块不是FREE状态的情况
    block->state = ZC_BLOCK_STATE_USING;
    result = zc_acquire_block_for_writing(block, size, writer_id);
    assert(result == ZC_INTERNAL_RUN_BLOCK_UNEXPECTED);
    printf("  Passed block not FREE state test\n");
    
    // 测试块空间不足的情况
    block->state = ZC_BLOCK_STATE_FREE;
    block->lut_offset = size - 100; // 设置为小于所需空间
    result = zc_acquire_block_for_writing(block, size, writer_id);
    assert(result == ZC_INTERNAL_RUN_BLOCK_UNEXPECTED);
    printf("  Passed insufficient space test\n");
    
    free(pages);
}

void test_zc_acquire_block_for_reading() {
    printf("Testing zc_acquire_block_for_reading...\n");

    // 创建测试用的页面和块
    int page_count = 5;
    zc_page_t* pages = create_test_pages(page_count);
    zc_block_header_t* block = (zc_block_header_t*)pages[0].data;
    
    // 首先初始化块
    zc_block_create_new_header(block, pages, page_count);
    
    // 设置块为USING状态并分配writer_id
    zc_writer_id_t writer_id = 5;
    block->state = ZC_BLOCK_STATE_USING;
    block->writer_id = writer_id;
    
    // 测试正常获取读取权限
    zc_reader_id_t reader_id = ((uint64_t)writer_id << 32) | 2; // 高32位是writer_id，低32位是reader索引
    zc_internal_result_t result = zc_acquire_block_for_reading(block, reader_id);
    
    // 验证结果
    assert(result == ZC_INTERNAL_OK);
    assert(block->reader_ref[(uint32_t)reader_id] == true);
    assert(block->reader_visited[(uint32_t)reader_id] == true);
    printf("  Passed normal zc_acquire_block_for_reading test\n");
    
    // 测试块不是USING状态的情况
    block->state = ZC_BLOCK_STATE_FREE;
    result = zc_acquire_block_for_reading(block, reader_id);
    assert(result == ZC_INTERNAL_RUN_BLOCK_UNEXPECTED);
    printf("  Passed block not USING state test\n");
    
    // 测试writer_id不匹配的情况
    block->state = ZC_BLOCK_STATE_USING;
    block->writer_id = writer_id + 1; // 设置不匹配的writer_id
    result = zc_acquire_block_for_reading(block, reader_id);
    assert(result == ZC_INTERNAL_RUN_BLOCK_UNEXPECTED);
    printf("  Passed writer_id mismatch test\n");
    
    free(pages);
}

void test_zc_acquire_block_for_cleaning() {
    printf("Testing zc_acquire_block_for_cleaning...\n");

    // 创建测试用的页面和块
    int page_count = 5;
    zc_page_t* pages = create_test_pages(page_count);
    zc_block_header_t* block = (zc_block_header_t*)pages[0].data;
    
    // 首先初始化块
    zc_block_create_new_header(block, pages, page_count);
    
    // 设置块为USING状态
    block->state = ZC_BLOCK_STATE_USING;
    
    // 测试正常获取清理权限
    zc_internal_result_t result = zc_acquire_block_for_cleaning(block);
    
    // 验证结果
    assert(result == ZC_INTERNAL_OK);
    assert(block->state == ZC_BLOCK_STATE_CLEAN);
    
    // 验证reader_visited被重置
    for (int i = 0; i < ZC_MAX_READERS_PER; i++) {
        assert(block->reader_visited[i] == false);
    }
    printf("  Passed normal zc_acquire_block_for_cleaning test\n");
    
    // 测试块不是USING状态的情况
    block->state = ZC_BLOCK_STATE_FREE;
    result = zc_acquire_block_for_cleaning(block);
    assert(result == ZC_INTERNAL_RUN_BLOCK_UNEXPECTED);
    printf("  Passed block not USING state test\n");
    
    free(pages);
}

int main() {
    printf("Starting block unit tests...\n\n");

    test_zc_block_create_new_header();
    test_zc_acquire_block_for_writing();
    test_zc_acquire_block_for_reading();
    test_zc_acquire_block_for_cleaning();

    printf("\nAll block unit tests passed!\n");
    return 0;
}