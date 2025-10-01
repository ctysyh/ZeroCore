#include "handle.h"

/**
 * 
 */
zc_internal_result_t zora_encrypt_handle(void* block_header_ptr,
    uint64_t key, uint8_t version, zc_handle_t* out_handle)
{
    if (!out_handle) return ZC_INTERNAL_PARAM_PTRNULL;

    out_handle->address = (uint64_t)block_header_ptr ^ key;
    out_handle->version = version ^ (key >> 60);
    return ZC_INTERNAL_OK;
}

/**
 * 
 */
zc_internal_result_t zora_decrypt_handle(zc_handle_t handle, uint64_t key,
    uint8_t expect_version, void** out_block_header_ptr)
{
    if (!out_block_header_ptr) return ZC_INTERNAL_PARAM_PTRNULL;

    uint8_t version = handle.version;
    version ^= (key >> 60);
    if (version != expect_version) return ZC_INTERNAL_ZORA_UNEXPECTVERSION;

    *out_block_header_ptr = (void*)(handle.address ^ key);
    return ZC_INTERNAL_OK;
}