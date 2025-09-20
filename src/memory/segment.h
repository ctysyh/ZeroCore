/*
*/

#ifndef SEGMENT_H
#define SEGMENT_H

#define ZC_VERSION_MAJOR 1
#define ZC_VERSION_MINOR 4

#include "zerocore_internal.h"
#include "page.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct zc_segment_stats {
    uint32_t state;
    float    usage_rate;
    float    block_avg_length;
    float    block_avg_reserve_time;
    int32_t  writer_access_count[ZC_MAX_WRITERS];
} zc_segment_stats_t;
#define ZC_SEGMENT_STATS_SIZE 144

typedef struct zc_segment_hardwork_info {

} zc_segment_hardwork_info_t;

typedef struct zc_page_stats {

} zc_page_stats_t;
#define ZC_PAGE_STATS_SIZE 32

typedef struct zc_segment {
    uint64_t seq;

    uint64_t content_page_count;
    zc_page_t* pages;
    zc_page_stats_t* pages_stats;

    zc_segment_stats_t stats;

    zc_segment_hardwork_info_t hardwork_info;
} zc_segment_t;

typedef enum zc_segment_state {
    ZC_SEGMENT_IDLE      = 0,
    ZC_SEGMENT_LOCK      = 1,
    ZC_SEGMENT_BUSY      = 2,
    ZC_SEGMENT_ERROR     = 3,
} zc_segment_state_t;



zc_internal_result_t zc_segment_create(
    zc_segment_t* seg
);

zc_internal_result_t zc_segment_lock(
    zc_segment_t* seg
);

zc_internal_result_t zc_segment_release(
    zc_segment_t* seg
);

#ifdef __cplusplus
}
#endif

#endif /* SEGMENT_H */