/*
*/
#pragma once

#ifndef TYPE_DESCRIPTOR_H
#define TYPE_DESCRIPTOR_H

#include "zerocore_internal.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum CorElementTypeEnum
{
    ELEMENT_TYPE_END            = 0x00,
    ELEMENT_TYPE_VOID           = 0x01,
    ELEMENT_TYPE_BOOLEAN        = 0x02,
    ELEMENT_TYPE_CHAR           = 0x03,
    ELEMENT_TYPE_I1             = 0x04,
    ELEMENT_TYPE_U1             = 0x05,
    ELEMENT_TYPE_I2             = 0x06,
    ELEMENT_TYPE_U2             = 0x07,
    ELEMENT_TYPE_I4             = 0x08,
    ELEMENT_TYPE_U4             = 0x09,
    ELEMENT_TYPE_I8             = 0x0A,
    ELEMENT_TYPE_U8             = 0x0B,
    ELEMENT_TYPE_R4             = 0x0C,
    ELEMENT_TYPE_R8             = 0x0D,
    ELEMENT_TYPE_STRING         = 0x0E,
    ELEMENT_TYPE_PTR            = 0x0F,
    ELEMENT_TYPE_BYREF          = 0x10,
    ELEMENT_TYPE_VALUETYPE      = 0x11,
    ELEMENT_TYPE_CLASS          = 0x12,
    ELEMENT_TYPE_VAR            = 0x13,
    ELEMENT_TYPE_ARRAY          = 0x14,
    ELEMENT_TYPE_I              = 0x18,
    ELEMENT_TYPE_U              = 0x19,
    ELEMENT_TYPE_OBJECT         = 0x1C,
    ELEMENT_TYPE_SZARRAY        = 0x1D,
    ELEMENT_TYPE_INTERNAL       = 0x21,

    ELEMENT_TYPE_SEPARATOR      = 0x40,
    ELEMENT_TYPE_PREFIX         = 0x80,
    ELEMENT_TYPE_SIGNEDASCII    = 0x44,
    ELEMENT_TYPE_ASCII          = 0x45,
    ELEMENT_TYPE_FLOATTENSOR    = 0x4C,
    ELEMENT_TYPE_FIXEDPOINT     = 0x4D,
    ELEMENT_TYPE_ASCIISTRING    = 0x4E,
    ELEMENT_TYPE_RAWBITS        = 0x5C,
} CorElementType;

/**
 * 浮点数的表示，统一采取4bit + 4bit的方案。高4bit表示大类，尤其是size；低4bit表示具体类型，特别地最低1bit表示是否有前导1
 */

typedef enum R4TypeTokenEnum
{
    R4_TYPE_FLOAT               = 0x00,  // E8M23 with implied leading 1

    R4_TYPE_HALF                = 0x01,  // E5M10 with implied leading 1
    R4_TYPE_BFLOAT16            = 0x11,  // E8M7 with implied leading 1

    R4_TYPE_TF32                = 0x02,  // E8M10 with implied leading 1

    R4_TYPE_FP8_E5M2            = 0x03,  // E5M2 with implied leading 1
    R4_TYPE_FP8_E4M3            = 0x13,  // E4M3 with implied leading 1

    R4_TYPE_FP6_E3M2            = 0x04,  // E3M2 with implied leading 1
    R4_TYPE_FP6_E2M3            = 0x14,  // E2M3 with implied leading 1
    R4_TYPE_FP6_E4M1            = 0x24,  // E4M1 with implied leading 1
    R4_TYPE_FP6_E2M3_NOLEADING  = 0x94,  // E2M3 without implied leading 1
} R4TypeToken;

typedef enum VecTypeTokenEnum
{
    VEC_TYPE_
} VecTypeToken;

typedef enum SqMatTypeTokenEnum
{
    SQMAT_TYPE_
} SqMatTypeToken;

typedef enum TensorTypeTokenEnum
{
    TENSOR_TYPE_
} TensorTypeToken;

typedef enum R8TypeTokenEnum
{
    R8_TYPE_DOUBLE              = 0x00,  // E11M52 with implied leading 1
    R8_TYPE_LONGDOUBLE          = 0x01,  // E15M64 with implied leading 1
    R8_TYPE_QUADRUPLE           = 0x02,  // E15M112 with implied leading 1
} R8TypeToken;

typedef enum FixpTypeTokenEnum
{
    FIXP_TYPE_
} FixpTypeToken;

zc_internal_result_t zc_get_type_desc_len(
    const uint8_t* desc,
    uint64_t* out_desc_len
);

zc_internal_result_t zc_type_desc_get_obj_size(
    const uint8_t* desc,
    uint64_t desc_len,
    uint64_t* out_obj_size
);

zc_internal_result_t inline zc_type_get_r4_obj_size(
    const uint8_t r4_type_token,
    uint64_t* out_obj_size
);

zc_internal_result_t inline zc_type_get_vector_obj_size(
    const uint8_t vec_type_token,
    const uint16_t vec_dim_count,
    uint64_t* out_obj_size
);

zc_internal_result_t inline zc_type_get_sqmatrix_obj_size(
    const uint8_t mat_type_token,
    const uint16_t mat_dim_count,
    uint64_t* out_obj_size
);

zc_internal_result_t inline zc_type_get_tensor_element_size(
    const uint8_t tensor_type_token,
    uint64_t* out_element_size
);

zc_internal_result_t inline zc_type_get_r8_obj_size(
    const uint8_t r8_type_token,
    uint64_t* out_obj_size
);

zc_internal_result_t inline zc_type_get_fixpoint_obj_size(
    const uint8_t fixp_type_token,
    uint64_t* out_obj_size
);

#ifdef __cplusplus
}
#endif

#endif /* TYPE_DESCRIPTOR_H */