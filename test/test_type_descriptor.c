#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "type/type_descriptor.h"

// 测试辅助函数
void test_simple_types() {
    size_t obj_size;
    uint8_t desc[10];
    zc_internal_result_t result;

    // 测试 ELEMENT_TYPE_BOOLEAN
    desc[0] = ELEMENT_TYPE_BOOLEAN;
    result = zc_type_desc_get_size(desc, 1, &obj_size);
    if (result == ZC_INTERNAL_OK && obj_size == 1) {
        printf("PASS: ELEMENT_TYPE_BOOLEAN size = 1\n");
    } else {
        printf("FAIL: ELEMENT_TYPE_BOOLEAN\n");
    }

    // 测试 ELEMENT_TYPE_CHAR
    desc[0] = ELEMENT_TYPE_CHAR;
    result = zc_type_desc_get_size(desc, 1, &obj_size);
    if (result == ZC_INTERNAL_OK && obj_size == 2) {
        printf("PASS: ELEMENT_TYPE_CHAR size = 2\n");
    } else {
        printf("FAIL: ELEMENT_TYPE_CHAR\n");
    }

    // 测试 ELEMENT_TYPE_I1
    desc[0] = ELEMENT_TYPE_I1;
    result = zc_type_desc_get_size(desc, 1, &obj_size);
    if (result == ZC_INTERNAL_OK && obj_size == 1) {
        printf("PASS: ELEMENT_TYPE_I1 size = 1\n");
    } else {
        printf("FAIL: ELEMENT_TYPE_I1\n");
    }

    // 测试 ELEMENT_TYPE_I2
    desc[0] = ELEMENT_TYPE_I2;
    result = zc_type_desc_get_size(desc, 1, &obj_size);
    if (result == ZC_INTERNAL_OK && obj_size == 2) {
        printf("PASS: ELEMENT_TYPE_I2 size = 2\n");
    } else {
        printf("FAIL: ELEMENT_TYPE_I2\n");
    }

    // 测试 ELEMENT_TYPE_I4
    desc[0] = ELEMENT_TYPE_I4;
    result = zc_type_desc_get_size(desc, 1, &obj_size);
    if (result == ZC_INTERNAL_OK && obj_size == 4) {
        printf("PASS: ELEMENT_TYPE_I4 size = 4\n");
    } else {
        printf("FAIL: ELEMENT_TYPE_I4\n");
    }

    // 测试 ELEMENT_TYPE_I8
    desc[0] = ELEMENT_TYPE_I8;
    result = zc_type_desc_get_size(desc, 1, &obj_size);
    if (result == ZC_INTERNAL_OK && obj_size == 8) {
        printf("PASS: ELEMENT_TYPE_I8 size = 8\n");
    } else {
        printf("FAIL: ELEMENT_TYPE_I8\n");
    }

    // 测试 ELEMENT_TYPE_R4
    desc[0] = ELEMENT_TYPE_R4;
    result = zc_type_desc_get_size(desc, 1, &obj_size);
    if (result == ZC_INTERNAL_OK && obj_size == 4) {
        printf("PASS: ELEMENT_TYPE_R4 size = 4\n");
    } else {
        printf("FAIL: ELEMENT_TYPE_R4\n");
    }

    // 测试 ELEMENT_TYPE_R8
    desc[0] = ELEMENT_TYPE_R8;
    result = zc_type_desc_get_size(desc, 1, &obj_size);
    if (result == ZC_INTERNAL_OK && obj_size == 8) {
        printf("PASS: ELEMENT_TYPE_R8 size = 8\n");
    } else {
        printf("FAIL: ELEMENT_TYPE_R8\n");
    }

    // 测试 ELEMENT_TYPE_VAR
    desc[0] = ELEMENT_TYPE_VAR;
    result = zc_type_desc_get_size(desc, 1, &obj_size);
    if (result == ZC_INTERNAL_OK && obj_size == 1) {
        printf("PASS: ELEMENT_TYPE_VAR size = 1\n");
    } else {
        printf("FAIL: ELEMENT_TYPE_VAR\n");
    }
}

void test_void_type() {
    size_t obj_size;
    uint8_t desc[10];
    zc_internal_result_t result;

    // 测试 ELEMENT_TYPE_VOID with width
    desc[0] = ELEMENT_TYPE_VOID;
    desc[1] = 16; // width = 16
    result = zc_type_desc_get_size(desc, 2, &obj_size);
    if (result == ZC_INTERNAL_OK && obj_size == 16) {
        printf("PASS: ELEMENT_TYPE_VOID with width = 16\n");
    } else {
        printf("FAIL: ELEMENT_TYPE_VOID with width\n");
    }

    // 测试 ELEMENT_TYPE_VOID without width
    desc[0] = ELEMENT_TYPE_VOID;
    result = zc_type_desc_get_size(desc, 1, &obj_size);
    if (result == ZC_INTERNAL_TYPE_ILLEGAL_DESC) {
        printf("PASS: ELEMENT_TYPE_VOID without width correctly returns error\n");
    } else {
        printf("FAIL: ELEMENT_TYPE_VOID without width should return error\n");
    }
}

void test_string_type() {
    size_t obj_size;
    uint8_t desc[10];
    zc_internal_result_t result;

    // 测试 ELEMENT_TYPE_STRING with length
    desc[0] = ELEMENT_TYPE_STRING;
    *((uint32_t*)(desc + 1)) = 10; // length = 10 chars
    result = zc_type_desc_get_size(desc, 5, &obj_size);
    if (result == ZC_INTERNAL_OK && obj_size == 20) { // 10 chars * 2 bytes each
        printf("PASS: ELEMENT_TYPE_STRING with length = 10 chars (20 bytes)\n");
    } else {
        printf("FAIL: ELEMENT_TYPE_STRING with length\n");
    }

    // 测试 ELEMENT_TYPE_STRING without length
    desc[0] = ELEMENT_TYPE_STRING;
    result = zc_type_desc_get_size(desc, 1, &obj_size);
    if (result == ZC_INTERNAL_TYPE_ILLEGAL_DESC) {
        printf("PASS: ELEMENT_TYPE_STRING without length correctly returns error\n");
    } else {
        printf("FAIL: ELEMENT_TYPE_STRING without length should return error\n");
    }
}

void test_ptr_type() {
    size_t obj_size;
    uint8_t desc[10];
    zc_internal_result_t result;

    // 测试 ELEMENT_TYPE_PTR
    desc[0] = ELEMENT_TYPE_PTR;
    result = zc_type_desc_get_size(desc, 1, &obj_size);
    if (result == ZC_INTERNAL_OK && obj_size == 8) {
        printf("PASS: ELEMENT_TYPE_PTR size = 8\n");
    } else {
        printf("FAIL: ELEMENT_TYPE_PTR\n");
    }
}

void test_platform_types() {
    size_t obj_size;
    uint8_t desc[10];
    zc_internal_result_t result;

    // 测试 ELEMENT_TYPE_I with width
    desc[0] = ELEMENT_TYPE_I;
    desc[1] = 4; // width = 4 bytes
    result = zc_type_desc_get_size(desc, 2, &obj_size);
    if (result == ZC_INTERNAL_OK && obj_size == 4) {
        printf("PASS: ELEMENT_TYPE_I with width = 4\n");
    } else {
        printf("FAIL: ELEMENT_TYPE_I with width\n");
    }

    // 测试 ELEMENT_TYPE_I without width
    desc[0] = ELEMENT_TYPE_I;
    result = zc_type_desc_get_size(desc, 1, &obj_size);
    if (result == ZC_INTERNAL_PARAM_ERROR) {
        printf("PASS: ELEMENT_TYPE_I without width correctly returns error\n");
    } else {
        printf("FAIL: ELEMENT_TYPE_I without width should return error\n");
    }

    // 测试 ELEMENT_TYPE_U with width
    desc[0] = ELEMENT_TYPE_U;
    desc[1] = 8; // width = 8 bytes
    result = zc_type_desc_get_size(desc, 2, &obj_size);
    if (result == ZC_INTERNAL_OK && obj_size == 8) {
        printf("PASS: ELEMENT_TYPE_U with width = 8\n");
    } else {
        printf("FAIL: ELEMENT_TYPE_U with width\n");
    }
}

void test_object_type() {
    size_t obj_size;
    uint8_t desc[10];
    zc_internal_result_t result;

    // 测试 ELEMENT_TYPE_OBJECT with size
    desc[0] = ELEMENT_TYPE_OBJECT;
    *((uint64_t*)(desc + 1)) = 128; // size = 128 bytes
    result = zc_type_desc_get_size(desc, 9, &obj_size);
    if (result == ZC_INTERNAL_OK && obj_size == 128) {
        printf("PASS: ELEMENT_TYPE_OBJECT with size = 128\n");
    } else {
        printf("FAIL: ELEMENT_TYPE_OBJECT with size\n");
    }

    // 测试 ELEMENT_TYPE_OBJECT without size
    desc[0] = ELEMENT_TYPE_OBJECT;
    result = zc_type_desc_get_size(desc, 1, &obj_size);
    if (result == ZC_INTERNAL_PARAM_ERROR) {
        printf("PASS: ELEMENT_TYPE_OBJECT without size correctly returns error\n");
    } else {
        printf("FAIL: ELEMENT_TYPE_OBJECT without size should return error\n");
    }
}

void test_internal_type() {
    size_t obj_size;
    uint8_t desc[10];
    zc_internal_result_t result;

    // 测试 ELEMENT_TYPE_INTERNAL
    desc[0] = ELEMENT_TYPE_INTERNAL;
    result = zc_type_desc_get_size(desc, 1, &obj_size);
    if (result == ZC_INTERNAL_OK && obj_size == 0) {
        printf("PASS: ELEMENT_TYPE_INTERNAL size = 0\n");
    } else {
        printf("FAIL: ELEMENT_TYPE_INTERNAL\n");
    }
}

void test_szarray_type() {
    size_t obj_size;
    uint8_t desc[10];
    zc_internal_result_t result;

    // 测试 ELEMENT_TYPE_SZARRAY
    desc[0] = ELEMENT_TYPE_SZARRAY;
    result = zc_type_desc_get_size(desc, 1, &obj_size);
    if (result == ZC_INTERNAL_OK && obj_size == 0) {
        printf("PASS: ELEMENT_TYPE_SZARRAY size = 0 (dynamic)\n");
    } else {
        printf("FAIL: ELEMENT_TYPE_SZARRAY\n");
    }
}

int main() {
    printf("Starting type_descriptor unit tests...\n\n");

    test_simple_types();
    test_void_type();
    test_string_type();
    test_ptr_type();
    test_platform_types();
    test_object_type();
    test_internal_type();
    test_szarray_type();

    printf("\nUnit tests completed.\n");
    return 0;
}