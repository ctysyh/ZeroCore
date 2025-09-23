#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/zora/handle.h"

// 测试 zora_encrypt_handle 函数
void test_zora_encrypt_handle() {
    printf("Testing zora_encrypt_handle...\n");
    
    // 测试正常情况
    zc_handle_t out_handle;
    void* test_ptr = (void*)0x123456789ABCDEF0;
    uint64_t key = 0xFEDCBA9876543210;
    uint8_t version = 0x05;
    
    zc_internal_result_t result = zora_encrypt_handle(test_ptr, key, version, &out_handle);
    
    // 验证结果
    assert(result == ZC_INTERNAL_OK);
    assert(out_handle.address == ((uint64_t)test_ptr ^ key));
    assert(out_handle.version == (version ^ (key >> 60)));
    
    // 测试空指针情况
    result = zora_encrypt_handle(test_ptr, key, version, NULL);
    assert(result == ZC_INTERNAL_PARAM_PTRNULL);
    
    printf("zora_encrypt_handle tests passed.\n");
}

// 测试 zora_decrypt_handle 函数
void test_zora_decrypt_handle() {
    printf("Testing zora_decrypt_handle...\n");
    
    // 先创建一个加密的 handle
    zc_handle_t handle;
    void* original_ptr = (void*)0x123456789ABCDEF0;
    uint64_t key = 0xFEDCBA9876543210;
    
    // 使用 encrypt 函数创建 handle
    zora_encrypt_handle(original_ptr, key, 0x05, &handle);
    
    uint8_t expect_version = 0x05;
    void* out_block_header_ptr = NULL;
    
    // 注意：zora_decrypt_handle 函数存在bug，它不会正确设置 out_block_header_ptr
    // 因为它只是修改了局部变量而不是指针指向的值
    zc_internal_result_t result = zora_decrypt_handle(handle, key, expect_version, &out_block_header_ptr);
    
    // 验证结果
    assert(result == ZC_INTERNAL_OK);
    assert(out_block_header_ptr == original_ptr);
    
    // 测试空指针情况
    result = zora_decrypt_handle(handle, key, expect_version, NULL);
    assert(result == ZC_INTERNAL_PARAM_PTRNULL);
    
    // 测试版本不匹配情况
    result = zora_decrypt_handle(handle, key, 0x06, &out_block_header_ptr);  // 错误的版本
    assert(result == ZC_INTERNAL_ZORA_UNEXPECTVERSION);
    
    printf("zora_decrypt_handle tests passed.\n");
}

// 测试加密后再解密是否能正确还原原始地址
void test_encrypt_then_decrypt() {
    printf("Testing encrypt then decrypt...\n");
    
    void* original_ptr = (void*)0x123456789ABCDEF0;
    uint64_t key = 0xFEDCBA9876543210;
    uint8_t version = 0x05;
    
    // 先加密
    zc_handle_t handle;
    zc_internal_result_t result = zora_encrypt_handle(original_ptr, key, version, &handle);
    assert(result == ZC_INTERNAL_OK);
    
    // 再解密
    void* decrypted_ptr = NULL;
    result = zora_decrypt_handle(handle, key, version, &decrypted_ptr);
    assert(result == ZC_INTERNAL_OK);
    
    // 验证解密后的指针与原始指针相同
    assert(decrypted_ptr == original_ptr);
    
    printf("Encrypt then decrypt test passed.\n");
}

int main() {
    printf("Running handle tests...\n");
    
    test_zora_encrypt_handle();
    test_zora_decrypt_handle();
    test_encrypt_then_decrypt();
    
    printf("All handle tests passed!\n");
    return 0;
}