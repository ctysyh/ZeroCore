/*
*/
#pragma once

#include <stdatomic.h>
#include "zerocore_internal.h"


#ifdef __cplusplus
extern "C" {
#endif

typedef struct zc_page_header
{
    uint64_t  line_seq : 61;    // 行序号
    uint64_t  state    : 3;     // 状态标识
    uint64_t  prev_page_addr;   // 前一页物理地址
} zc_page_header_t;
#ifndef ZC_PAGE_HEADER_SIZE
#define ZC_PAGE_HEADER_SIZE sizeof(zc_page_header_t)
#endif

typedef struct zc_page_tail
{
    uint64_t  next_page_addr;   // 下一页物理地址
} zc_page_tail_t;
#ifndef ZC_PAGE_TAIL_SIZE
#define ZC_PAGE_TAIL_SIZE sizeof(zc_page_tail_t)
#endif

#ifndef ZC_PAGE_DATA_SIZE
#define ZC_PAGE_DATA_SIZE 488
#endif
typedef struct zc_page
{
    zc_page_header_t header;
    char             data[ZC_PAGE_DATA_SIZE];
    zc_page_tail_t   tail;
} zc_page_t;
#ifndef ZC_PAGE_SIZE
#define ZC_PAGE_SIZE sizeof(zc_page_t)
#endif

typedef enum zc_page_state
{
    ZC_PAGE_STATE_IDLE      = 0,
    ZC_PAGE_STATE_AS_HEAD   = 1,
    ZC_PAGE_STATE_AS_MID    = 2,
    ZC_PAGE_STATE_AS_DTTA   = 3,

    ZC_PAGE_STATE_LOCK      = 5,
    ZC_PAGE_STATE_BUSY      = 6,
    ZC_PAGE_STATE_ERROR     = 7
} zc_page_state_t; // 总共有3位即8个可表示的状态

#ifdef __cplusplus
}
#endif