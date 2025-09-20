/*
*/

#ifndef PAGE_H
#define PAGE_H

#define ZC_VERSION_MAJOR 1
#define ZC_VERSION_MINOR 4

#include "zerocore_internal.h"
#include "pool.h"
#include "segment.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct zc_page {
    zc_page_header_t header;
    char             data[488];
    zc_page_tail_t   tail;
} zc_page_t;
#define ZC_PAGE_SIZE 512

typedef struct zc_page_header {
    uint64_t  line_seq : 61;    // 行序号
    uint64_t  state    : 3;     // 状态标识
    uint64_t  prev_page_addr;   // 前一页物理地址
} zc_page_header_t;

typedef struct zc_page_tail {
    uint64_t  next_page_addr;   // 下一页物理地址
} zc_page_tail_t;

typedef enum zc_page_state {
    ZC_PAGE_IDLE      = 0,
    ZC_PAGE_AS_HEAD   = 1,
    ZC_PAGE_AS_MID    = 2,
    ZC_PAGE_AS_DTTA   = 3,

    ZC_PAGE_LOCK      = 5,
    ZC_PAGE_BUSY      = 6,
    ZC_PAGE_ERROR     = 7
} zc_page_state_t; // 总共有3位即8个可表示的状态

#ifdef __cplusplus
}
#endif

#endif /* PAGE_H */