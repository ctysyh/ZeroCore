#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include "../src/memory/segment.h"

void test_zc_segment_create() {
    printf("Testing zc_segment_create...\n");

    // 测试空指针情况
    zc_internal_result_t result = zc_segment_create(NULL);
    assert(result == ZC_INTERNAL_PARAM_PTRNULL);
    printf("  Passed NULL pointer test\n");

    // 测试正常情况
    zc_segment_t* seg = malloc(sizeof(zc_segment_t));
    seg->content_page_count = 2; // 创建一个包含2个页面的段

    result = zc_segment_create(seg);
    assert(result == ZC_INTERNAL_OK);
    assert(seg->pages != NULL);
    assert(seg->pages_stats != NULL);
    printf("  Passed normal creation test\n");

    // 清理测试资源
    free(seg->pages);
    free(seg->pages_stats);
    free(seg);

    printf("zc_segment_create tests passed!\n\n");
}

void test_zc_segment_lock() {
    printf("Testing zc_segment_lock...\n");

    // 创建一个段用于测试
    zc_segment_t* seg = malloc(sizeof(zc_segment_t));
    seg->content_page_count = 2;

    zc_internal_result_t result = zc_segment_create(seg);
    assert(result == ZC_INTERNAL_OK);

    // 锁定段
    result = zc_segment_lock(seg);
    assert(result == ZC_INTERNAL_OK);
    assert(seg->stats.state == ZC_SEGMENT_BUSY);

    // 检查所有页面是否都被标记为BUSY
    for (uint64_t i = 0; i < seg->content_page_count; i++) {
        zc_page_t* page = seg->pages + i;
        assert(page->header.state == ZC_PAGE_STATE_BUSY);
    }
    printf("  Passed segment lock test\n");

    // 清理测试资源
    free(seg->pages);
    free(seg->pages_stats);
    free(seg);

    printf("zc_segment_lock tests passed!\n\n");
}

void test_zc_segment_release() {
    printf("Testing zc_segment_release...\n");

    // 创建一个段用于测试
    zc_segment_t* seg = malloc(sizeof(zc_segment_t));
    seg->content_page_count = 2;

    zc_internal_result_t result = zc_segment_create(seg);
    assert(result == ZC_INTERNAL_OK);

    // 释放段
    result = zc_segment_release(seg);
    assert(result == ZC_INTERNAL_OK);
    printf("  Passed segment release test\n");

    printf("zc_segment_release tests passed!\n\n");
}

int main() {
    printf("Starting segment unit tests...\n\n");

    test_zc_segment_create();
    test_zc_segment_lock();
    test_zc_segment_release();

    printf("All segment unit tests passed!\n");
    return 0;
}