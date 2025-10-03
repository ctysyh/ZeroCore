#include "type_descriptor.h"
#include <stdlib.h>

/**
 * 获取类型描述符长度。
 * 实际上，这个函数是局部性的，
 * 它不能识别这个desc是否是一个顶级变量的desc，但是它能识别这个desc的最小完整边界。
 * 
 * @param desc 类型描述符
 * @param out_desc_len 类型描述符长度
 */
zc_internal_result_t zc_get_type_desc_len(const uint8_t* desc, uint64_t* out_desc_len)
{
    if (unlikely(!desc)) return ZC_INTERNAL_TYPE_ILLEGAL_DESC;

    uint8_t type_tag = desc[0];
    uint64_t len = 1; // 至少包含tag本身

    switch (type_tag)
    {
        case ELEMENT_TYPE_END:
        case ELEMENT_TYPE_SEPARATOR:
        case ELEMENT_TYPE_PREFIX:
        case ELEMENT_TYPE_BOOLEAN:
        case ELEMENT_TYPE_CHAR:
        case ELEMENT_TYPE_I1:
        case ELEMENT_TYPE_SIGNEDASCII:
        case ELEMENT_TYPE_U1:
        case ELEMENT_TYPE_ASCII:
        case ELEMENT_TYPE_I2:
        case ELEMENT_TYPE_U2:
        case ELEMENT_TYPE_I4:
        case ELEMENT_TYPE_U4:
        case ELEMENT_TYPE_I8:
        case ELEMENT_TYPE_U8:
        case ELEMENT_TYPE_VAR:
        case ELEMENT_TYPE_INTERNAL:
        {
            // 这些类型只有tag，没有附加tokens
            break;
        }

        case ELEMENT_TYPE_VOID:
        case ELEMENT_TYPE_R4:
        case ELEMENT_TYPE_R8:
        case ELEMENT_TYPE_FIXEDPOINT:
        case ELEMENT_TYPE_I:
        case ELEMENT_TYPE_U:
        {
            len += 1;
            break;
        }

        case ELEMENT_TYPE_FLOATTENSOR:
        {
            uint16_t d = *(uint16_t*)(desc + 2);
            len += (3 + 2 * d);
        }

        case ELEMENT_TYPE_STRING:
        case ELEMENT_TYPE_ASCIISTRING:
        {
            len += 4; // 字符数量Length
            break;
        }

        case ELEMENT_TYPE_PTR:
        case ELEMENT_TYPE_OBJECT:
        case ELEMENT_TYPE_RAWBITS:
        {
            len += 8;
            break;
        }

        case ELEMENT_TYPE_BYREF:
        {
            len += 9;
            break;
        }

        case ELEMENT_TYPE_VALUETYPE:
        case ELEMENT_TYPE_CLASS:
        {
            uint32_t tokens_len = *(uint32_t*)(desc + 1);
            len += tokens_len;
            break;
        }

        case ELEMENT_TYPE_ARRAY:
        {
            uint64_t elem_desc_len;
            zc_internal_result_t res = zc_get_type_desc_len(desc + 1, &elem_desc_len);
            if (res != ZC_INTERNAL_OK) return res;
            len += elem_desc_len;

            uint8_t dim_count = *(uint8_t*)(desc + len);
            len += dim_count * (8 + 8);
            break;
        }

        case ELEMENT_TYPE_SZARRAY:
        {
            uint64_t elem_type_len;
            zc_internal_result_t res = zc_get_type_desc_len(desc + 1, &elem_type_len);
            if (res != ZC_INTERNAL_OK) return res;
            len += elem_type_len + 4;
            break;
        }
            
        default:
        {
            return ZC_INTERNAL_UNREALIZED;
        }
    }
    
    *out_desc_len = len;
    return ZC_INTERNAL_OK;
}

/**
 * 根据类型描述符获取变量长度
 * @param desc 类型描述符
 * @param desc_len 类型描述符长度
 * @param out_size 变量长度
 */
zc_internal_result_t zc_type_desc_get_obj_size(const uint8_t* desc,
    uint64_t desc_len, uint64_t* out_obj_size)
{
    if (!desc || !out_obj_size || desc_len == 0) return ZC_INTERNAL_PARAM_PTRNULL;

    uint8_t type_tag = desc[0];

    switch (type_tag)
    {
        case ELEMENT_TYPE_END:
        case ELEMENT_TYPE_SEPARATOR:
        case ELEMENT_TYPE_PREFIX:
        {
            if (unlikely(desc_len != 1)) return ZC_INTERNAL_TYPE_ILLEGAL_DESC;
            *out_obj_size = 0;
            break;
        }

        case ELEMENT_TYPE_VOID:
        {
            if (unlikely(desc_len != 2)) return ZC_INTERNAL_TYPE_ILLEGAL_DESC;
            uint8_t exp = *(desc + 1);
            if (exp == 0) *out_obj_size = 0;
            else
            {
                *out_obj_size = (1U << (exp - 1U));
            }

            break;
        }

        case ELEMENT_TYPE_BOOLEAN:
        {
            if (unlikely(desc_len != 1)) return ZC_INTERNAL_TYPE_ILLEGAL_DESC;
            *out_obj_size = 1;
            break;
        }

        case ELEMENT_TYPE_CHAR:
        {
            if (unlikely(desc_len != 1)) return ZC_INTERNAL_TYPE_ILLEGAL_DESC;
            *out_obj_size = 2;
            break;
        }

        case ELEMENT_TYPE_I1:
        case ELEMENT_TYPE_SIGNEDASCII:
        case ELEMENT_TYPE_U1:
        case ELEMENT_TYPE_ASCII:
        {
            if (unlikely(desc_len != 1)) return ZC_INTERNAL_TYPE_ILLEGAL_DESC;
            *out_obj_size = 1;
            break;
        }

        case ELEMENT_TYPE_I2:
        case ELEMENT_TYPE_U2:
        {
            if (unlikely(desc_len != 1)) return ZC_INTERNAL_TYPE_ILLEGAL_DESC;
            *out_obj_size = 2;
            break;
        }

        case ELEMENT_TYPE_I4:
        case ELEMENT_TYPE_U4:
        {
            if (unlikely(desc_len != 1)) return ZC_INTERNAL_TYPE_ILLEGAL_DESC;
            *out_obj_size = 4;
            break;
        }

        case ELEMENT_TYPE_I8:
        case ELEMENT_TYPE_U8:
        {
            if (unlikely(desc_len != 1)) return ZC_INTERNAL_TYPE_ILLEGAL_DESC;
            *out_obj_size = 8;
            break;
        }

        case ELEMENT_TYPE_R4:
        {
            if (unlikely(desc_len != 2)) return ZC_INTERNAL_TYPE_ILLEGAL_DESC;
            zc_internal_result_t res = zc_type_get_r4_obj_size(desc[1], out_obj_size);
            if (res != ZC_INTERNAL_OK) return res;
            break;
        }

        case ELEMENT_TYPE_FLOATTENSOR:
        {
            uint16_t o = *(uint16_t*)(desc + 2);
            if (unlikely(desc_len != o * 2 + 4)) return ZC_INTERNAL_TYPE_ILLEGAL_DESC;

            if (o == 1)
            {
                uint16_t d = *(uint16_t*)(desc + 4);
                zc_internal_result_t res = zc_type_get_vector_obj_size(desc[1], d, out_obj_size);
                if (res != ZC_INTERNAL_OK) return res;
                break;
            }

            else if (o == 2)
            {
                uint16_t d0 = *(uint16_t*)(desc + 4);
                uint16_t d1 = *(uint16_t*)(desc + 6);
                if (likely(d0 == d1))
                {
                    zc_internal_result_t res = zc_type_get_sqmatrix_obj_size(desc[1], d1, out_obj_size);
                    if (res != ZC_INTERNAL_OK) return res;
                    break;
                }
            }

            uint64_t element_size;
            zc_internal_result_t res = zc_type_get_tensor_element_size(desc[1], &element_size);
            if (res != ZC_INTERNAL_OK) return res;
            uint64_t element_count = 0;
            for (uint16_t i = 0; i < o; i++)
            {
                element_count += *(uint16_t*)(desc + 4 + 2 * i);
            }
            *out_obj_size = element_size * element_count;
            break;
        }

        case ELEMENT_TYPE_R8:
        {
            if (unlikely(desc_len != 2)) return ZC_INTERNAL_TYPE_ILLEGAL_DESC;
            zc_internal_result_t res = zc_type_get_r8_obj_size(desc[1], out_obj_size);
            if (res != ZC_INTERNAL_OK) return res;
            break;
        }

        case ELEMENT_TYPE_FIXEDPOINT:
        {
            if (unlikely(desc_len != 2)) return ZC_INTERNAL_TYPE_ILLEGAL_DESC;
            zc_internal_result_t res = zc_type_get_fixpoint_obj_size(desc[1], out_obj_size);
            if (res != ZC_INTERNAL_OK) return res;
            break;
        }

        case ELEMENT_TYPE_STRING:
        {
            if (unlikely(desc_len != 5)) return ZC_INTERNAL_TYPE_ILLEGAL_DESC;
            uint32_t length = *(uint32_t*)(desc + 1);
            *out_obj_size = length * 2; // UTF-16LE每个字符2字节
            break;
        }

        case ELEMENT_TYPE_ASCIISTRING:
        {
            if (unlikely(desc_len != 5)) return ZC_INTERNAL_TYPE_ILLEGAL_DESC;
            *out_obj_size = *(uint32_t*)(desc + 1);
            break;
        }

        case ELEMENT_TYPE_PTR:
        {
            if (unlikely(desc_len != 9)) return ZC_INTERNAL_TYPE_ILLEGAL_DESC;
            *out_obj_size = 8;
            break;
        }

        case ELEMENT_TYPE_BYREF:
        {
            if (unlikely(desc_len != 10)) return ZC_INTERNAL_TYPE_ILLEGAL_DESC;
            *out_obj_size = 8;
            break;
        }

        case ELEMENT_TYPE_VALUETYPE:
        case ELEMENT_TYPE_CLASS:
        {
            if (unlikely(desc_len != *(uint32_t*)(desc + 1) + 1)) return ZC_INTERNAL_TYPE_ILLEGAL_DESC;
            *out_obj_size = *(uint64_t*)(desc + 5);
            break;
        }
            
        case ELEMENT_TYPE_VAR:
        {
            if (unlikely(desc_len != 1)) return ZC_INTERNAL_TYPE_ILLEGAL_DESC;
            *out_obj_size = 1;
            break;
        }
            
        case ELEMENT_TYPE_ARRAY:
        {
            uint64_t element_desc_len;
            zc_internal_result_t res = zc_get_type_desc_len(desc + 1, &element_desc_len);
            if (res != ZC_INTERNAL_OK) return res;

            uint8_t dim_count = *(uint8_t*)(desc + element_desc_len + 1);
            if (unlikely(desc_len != element_desc_len + 2 + dim_count * 16)) return ZC_INTERNAL_TYPE_ILLEGAL_DESC;

            uint64_t element_size;
            res = zc_type_desc_get_obj_size(desc + 1, element_desc_len, &element_size);
            if (res != ZC_INTERNAL_OK) return res;

            uint64_t element_count = 0;
            for (uint8_t i = 0; i < dim_count; i++)
            {
                element_count += *(uint64_t*)(desc + element_desc_len + (1 + 1 + 8) + 16 * i);
            }
            *out_obj_size = element_count * element_size;
            break;
        }
            
        case ELEMENT_TYPE_I:
        case ELEMENT_TYPE_U:
        {
            if (unlikely(desc_len != 2)) return ZC_INTERNAL_TYPE_ILLEGAL_DESC;
            uint8_t exp = *(desc + 1);
            *out_obj_size = (1U << exp);
            break;
        }
            
        case ELEMENT_TYPE_OBJECT:
        case ELEMENT_TYPE_RAWBITS:
        {
            if (unlikely(desc_len != 9)) return ZC_INTERNAL_TYPE_ILLEGAL_DESC;
            *out_obj_size = *(uint64_t*)(desc + 1);
            break;
        }
            
        case ELEMENT_TYPE_SZARRAY:
        {
            uint64_t elem_desc_len;
            zc_internal_result_t res = zc_get_type_desc_len(desc + 1, &elem_desc_len);
            if (res != ZC_INTERNAL_OK) return res;

            uint64_t single_elem_size = 0;
            res = zc_type_desc_get_size(desc + 1, elem_desc_len, &single_elem_size);
            if (res != ZC_INTERNAL_OK) return res;

            uint32_t total_count = *(uint32_t*)(desc + 1 + elem_desc_len);
            *out_obj_size = single_elem_size * total_count;
            break;
        }
            
        default:
        {
            return ZC_INTERNAL_UNREALIZED;
        }
    }
    
    return ZC_INTERNAL_OK;
}