#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include "../src/memory/block.h"
#include "../src/memory/segment.h"

void test_zc_block_offset_to_ptr_with_valid_offsets()
{
    printf("Testing zc_block_offset_to_ptr with valid offsets...\n");

    // 创建测试用的段，包含10页
    zc_segment_t segment;
    segment.content_page_count = 10;
    int32_t flag = zc_segment_create(&segment);
    if (flag != ZC_INTERNAL_OK)
    {
        printf("Failed to create segment: %d\n", flag);
        return;
    }

    zc_block_header_t* block = segment.pages[0].data;
    flag = zc_block_create(block, 2800, 7);
    
    // 测试第一页中的偏移量
    size_t offset = 100;
    void* ptr = zc_block_offset_to_ptr(block, offset);
    void* expected_ptr = (char*)block->page_cache[0] + ZC_PAGE_HEADER_SIZE + offset;
    assert(ptr == expected_ptr);
    printf("  Passed offset in first page test\n");
    
    // 测试第七页(最后一页缓存页)中的偏移量
    offset = 6 * ZC_PAGE_DATA_SIZE + 50; // 第7页中的第50字节
    ptr = zc_block_offset_to_ptr(block, offset);
    expected_ptr = (char*)block->page_cache[6] + ZC_PAGE_HEADER_SIZE + 50;
    assert(ptr == expected_ptr);
    printf("  Passed offset in last cached page test\n");
    
    // 测试第八页(非缓存页)中的偏移量
    offset = 7 * ZC_PAGE_DATA_SIZE + 100; // 第8页中的第100字节
    ptr = zc_block_offset_to_ptr(block, offset);
    expected_ptr = (char*)block->page_cache[7] + ZC_PAGE_HEADER_SIZE + 100;
    assert(ptr == expected_ptr);
    printf("  Passed offset in non-cached page test\n");
    
    // 测试最后一页中的偏移量
    offset = 9 * ZC_PAGE_DATA_SIZE + 200; // 第10页中的第200字节
    ptr = zc_block_offset_to_ptr(block, offset);
    expected_ptr = (char*)block->page_cache[9] + ZC_PAGE_HEADER_SIZE + 200;
    assert(ptr == expected_ptr);
    printf("  Passed offset in last page test\n");
    
    free(block->page_cache[0]);
}

void test_zc_block_offset_to_ptr_boundary_conditions() {
    printf("Testing zc_block_offset_to_ptr boundary conditions...\n");

    // 创建测试用的块
    int page_count = 5;
    zc_block_header_t* block = create_test_block(page_count, 0); // lut_enabled
    
    // 测试偏移量为0
    size_t offset = 0;
    void* ptr = zc_block_offset_to_ptr(block, offset);
    void* expected_ptr = (char*)block->page_cache[0] + ZC_PAGE_HEADER_SIZE;
    assert(ptr == expected_ptr);
    printf("  Passed offset 0 test\n");
    
    // 测试刚好在块边界内的最大偏移量
    offset = block->lut_offset + ZC_BLOCK_HEADER_SIZE + 
             sizeof(zc_dtt_lut_header_t) + (ZC_DTT_LUT_ENTRY_MAX_COUNT * ZC_DTT_LUT_ENTRY_SIZE) - 1;
    ptr = zc_block_offset_to_ptr(block, offset);
    assert(ptr != NULL);
    printf("  Passed maximum valid offset test\n");
    
    free(block->page_cache[0]);
}

void test_zc_block_offset_to_ptr_out_of_bounds() {
    printf("Testing zc_block_offset_to_ptr with out of bounds offsets...\n");

    // 创建测试用的块
    int page_count = 5;
    zc_block_header_t* block = create_test_block(page_count, 0); // lut_enabled
    
    // 测试超出块边界的偏移量
    size_t offset = block->lut_offset + ZC_BLOCK_HEADER_SIZE + 
                    sizeof(zc_dtt_lut_header_t) + (ZC_DTT_LUT_ENTRY_MAX_COUNT * ZC_DTT_LUT_ENTRY_SIZE);
    void* ptr = zc_block_offset_to_ptr(block, offset);
    assert(ptr == NULL);
    printf("  Passed out of bounds offset test\n");
    
    // 测试远超块边界的偏移量
    offset = block->lut_offset + ZC_BLOCK_HEADER_SIZE + 
             sizeof(zc_dtt_lut_header_t) + (ZC_DTT_LUT_ENTRY_MAX_COUNT * ZC_DTT_LUT_ENTRY_SIZE) + 1000;
    ptr = zc_block_offset_to_ptr(block, offset);
    assert(ptr == NULL);
    printf("  Passed far out of bounds offset test\n");
    
    free(block->page_cache[0]);
}

void test_zc_block_offset_to_ptr_with_disabled_lut() {
    printf("Testing zc_block_offset_to_ptr with disabled LUT...\n");

    // 创建测试用的块，LUT禁用
    int page_count = 3;
    zc_block_header_t* block = create_test_block(page_count, 1); // lut_disabled
    
    // 测试有效偏移量
    size_t offset = 150;
    void* ptr = zc_block_offset_to_ptr(block, offset);
    void* expected_ptr = (char*)block->page_cache[0] + ZC_PAGE_HEADER_SIZE + offset;
    assert(ptr == expected_ptr);
    printf("  Passed valid offset with disabled LUT test\n");
    
    // 测试刚好在块边界内的最大偏移量（LUT禁用时的边界）
    offset = block->lut_offset + ZC_BLOCK_HEADER_SIZE - 1;
    ptr = zc_block_offset_to_ptr(block, offset);
    assert(ptr != NULL);
    printf("  Passed maximum valid offset with disabled LUT test\n");
    
    // 测试刚好超出块边界的偏移量（LUT禁用时的边界）
    offset = block->lut_offset + ZC_BLOCK_HEADER_SIZE;
    ptr = zc_block_offset_to_ptr(block, offset);
    assert(ptr == NULL);
    printf("  Passed out of bounds offset with disabled LUT test\n");
    
    free(block->page_cache[0]);
}

void test_zc_block_offset_to_ptr_with_null_page() {
    printf("Testing zc_block_offset_to_ptr with null page...\n");

    // 创建测试用的块
    int page_count = 10;
    zc_block_header_t* block = create_test_block(page_count, 0);
    
    // 手动将某个页缓存设置为NULL以测试错误情况
    // 我们将第8页的指针设置为NULL
    zc_page_t** next_ptr = (zc_page_t**)((char*)block->page_cache[0] + ZC_PAGE_SIZE - 8);
    *next_ptr = NULL;
    
    // 测试需要访问第8页的偏移量
    size_t offset = 7 * ZC_PAGE_DATA_SIZE + 100; // 第8页中的第100字节
    void* ptr = zc_block_offset_to_ptr(block, offset);
    assert(ptr == NULL);
    printf("  Passed null page test\n");
    
    free(block->page_cache[0]);
}

int main() {
    printf("Starting zc_block_offset_to_ptr unit tests...\n\n");

    test_zc_block_offset_to_ptr_with_valid_offsets();
    test_zc_block_offset_to_ptr_boundary_conditions();
    test_zc_block_offset_to_ptr_out_of_bounds();
    test_zc_block_offset_to_ptr_with_disabled_lut();
    test_zc_block_offset_to_ptr_with_null_page();

    printf("\nAll zc_block_offset_to_ptr unit tests passed!\n");
    return 0;
}