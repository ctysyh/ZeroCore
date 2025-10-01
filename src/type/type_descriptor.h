/*
*/
#pragma once

#ifndef TYPE_DESCRIPTOR_H
#define TYPE_DESCRIPTOR_H

#include "zerocore_internal.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
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
    ELEMENT_TYPE_UNSIGNEDASCII  = 0x45,
    ELEMENT_TYPE_FIXEDPOINT     = 0x4D,
    ELEMENT_TYPE_RAWBITS        = 0x5C,
} CorElementType;

zc_internal_result_t zc_get_type_descriptor_length(
    const uint8_t* desc,
    uint64_t* out_desc_len
);

zc_internal_result_t zc_type_desc_get_obj_size(
    const uint8_t* desc,
    uint64_t desc_len,
    uint64_t* obj_size
);

#ifdef __cplusplus
}
#endif

#endif /* TYPE_DESCRIPTOR_H */