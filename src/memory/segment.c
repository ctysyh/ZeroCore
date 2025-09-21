/**/

#include <stdlib.h>
#include "segment.h"

/**
 * 
 */
zc_internal_result_t zc_segment_create(zc_segment_t* seg)
{
    if (unlikely(seg == NULL)) return ZC_INTERNAL_PARAM_PTRNULL;

    seg->pages = malloc(seg->content_page_count * ZC_PAGE_SIZE);
    if (unlikely(seg->pages == NULL)) return ZC_INTERNAL_RUN_PTRNULL;

    seg->pages_stats = malloc(seg->content_page_count * ZC_PAGE_STATS_SIZE);
    if (unlikely(seg->pages_stats == NULL)) return ZC_INTERNAL_RUN_PTRNULL;

    return ZC_INTERNAL_OK;
}

/**
 * 
 */
zc_internal_result_t zc_segment_lock(zc_segment_t* seg)
{
    // 将状态改成BUSY
    seg->stats.state = ZC_SEGMENT_BUSY;

    // 遍历页，将状态改成BUSY
    for (uint64_t i = 0; i < seg->content_page_count; i++) {
        zc_page_t* page = seg->pages + i;
        page->header.state = ZC_PAGE_STATE_BUSY;
    }

    // 遍历页，等待其状态变为LOCK

    return ZC_INTERNAL_OK;
}

/**
 * 
 */
zc_internal_result_t zc_segment_release(zc_segment_t* seg)
{
    int flag = zc_segment_lock(seg);
    if (unlikely(flag != ZC_INTERNAL_OK)) return flag;

    // 释放页
    free(seg->pages);

    // 释放页统计
    free(seg->pages_stats);

    // 释放段元数据
    free(seg);

    // 将seg改成NULL
    seg = NULL;

    return ZC_INTERNAL_OK;
}