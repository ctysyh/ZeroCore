/*
*/
#pragma once

#include "zerocore_internal.h"

#ifndef HANDLE_H
#define HANDLE_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct zc_handle
{
    uint64_t address : 60;
    uint64_t version : 4;
} zc_handle_t;

zc_internal_result_t zora_encrypt_handle(
    void* block_header_ptr,
    uint64_t key,
    uint8_t version,
    zc_handle_t* out_handle
);

zc_internal_result_t zora_decrypt_handle(
    zc_handle_t handle,
    uint64_t key,
    uint8_t version,
    void** out_block_header_ptr
);

#ifdef __cplusplus
}
#endif

#endif /* HANDLE_H */