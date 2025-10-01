#include "type_descriptor.h"
#include <stdlib.h>

/**
 * 获取类型描述符长度
 * @param desc 类型描述符
 * @param out_desc_len 类型描述符长度
 */
zc_internal_result_t zc_get_type_descriptor_length(
    const uint8_t* desc, uint64_t* out_desc_len)
{
    if (!desc) return ZC_INTERNAL_TYPE_ILLEGAL_DESC;

    uint8_t type_tag = desc[0];
    uint64_t len = 1; // 至少包含tag本身

    switch (type_tag) {
        case ELEMENT_TYPE_END:
        case ELEMENT_TYPE_BOOLEAN:
        case ELEMENT_TYPE_CHAR:
        case ELEMENT_TYPE_I1:
        case ELEMENT_TYPE_U1:
        case ELEMENT_TYPE_I2:
        case ELEMENT_TYPE_U2:
        case ELEMENT_TYPE_I4:
        case ELEMENT_TYPE_U4:
        case ELEMENT_TYPE_I8:
        case ELEMENT_TYPE_U8:
        case ELEMENT_TYPE_VAR:
        case ELEMENT_TYPE_INTERNAL:
        case ELEMENT_TYPE_SEPARATOR:
        case ELEMENT_TYPE_PREFIX:
        case ELEMENT_TYPE_SIGNEDASCII:
        case ELEMENT_TYPE_UNSIGNEDASCII:
        {
            // 这些类型只有tag，没有附加tokens
            break;
        }

        case ELEMENT_TYPE_VOID:
        case ELEMENT_TYPE_R4:
        case ELEMENT_TYPE_R8:
        case ELEMENT_TYPE_I:
        case ELEMENT_TYPE_U:
        case ELEMENT_TYPE_FIXEDPOINT:
        {
            len += 1;
            break;
        }

        case ELEMENT_TYPE_STRING:
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
            len += 9; // 类型描述符1字节 + 8字节长度
            break;
        }

        case ELEMENT_TYPE_VALUETYPE:
        {
            // 1字节对齐宽度 + 2字节字段数量 + N个字段描述符
            uint16_t field_count = *((uint16_t*)(desc + 2));
            len += 3; // 对齐宽度(1B) + 字段数量(2B)

            for (uint16_t i = 0; i < field_count; i++)
            {
                uint64_t field_len;
                int32_t flag = zc_get_type_descriptor_length(desc + len, &field_len);
                if (flag != ZC_INTERNAL_OK) return flag;
                len += field_len;
            }
            break;
        }

        case ELEMENT_TYPE_CLASS:
        {
            // 1字节字段数量 + 1字节Header大小 + Header + N个字段信息
            uint8_t field_count = desc[1];
            uint8_t header_size = desc[2];
            len += 1 + 1 + header_size; // 字段数量(1B) + Header大小(1B) + Header(wB)

            // N个字段信息：类型描述符 + 8字节偏移量 + 8字节大小
            for (uint8_t i = 0; i < field_count; i++)
            {
                uint64_t field_type_len;
                int32_t flag = zc_get_type_descriptor_length(desc + len, &field_type_len);
                if (flag != ZC_INTERNAL_OK) return flag;
                len += field_type_len + (8 + 8);
            }
            break;
        }

        case ELEMENT_TYPE_ARRAY:
        {
            // 1字节维度数量 + 元素类型描述符 + R个(8字节下界+8字节长度)
            uint8_t dimensions = desc[1];
            len += 1; // 维度数量

            // 元素类型描述符（递归）
            uint64_t elem_type_len;
            int32_t flag = zc_get_type_descriptor_length(desc + len, &elem_type_len);
            if (flag != ZC_INTERNAL_OK) return flag;
            len += elem_type_len;

            // R个维度信息
            len += dimensions * (8 + 8); // 每个维度：8字节下界 + 8字节长度
            break;
        }

        case ELEMENT_TYPE_SZARRAY:
        {
            // 元素类型描述符 + 4字节元素数量
            // 元素类型描述符（递归）
            uint64_t elem_type_len;
            int32_t flag = zc_get_type_descriptor_length(desc + 1, &elem_type_len);
            if (flag != ZC_INTERNAL_OK) return flag;
            len += elem_type_len + 4;
            break;
        }
            
        default:
        {
            return ZC_INTERNAL_TYPE_ILLEGAL_DESC;
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
    // 检查参数
    if (!desc || !out_obj_size || desc_len == 0) return ZC_INTERNAL_PARAM_PTRNULL;

    // 获取类型标签（第一个字节）
    uint8_t type_tag = desc[0];

    switch (type_tag)
    {
        case ELEMENT_TYPE_END:
        {
            // 语义：类型流结束标记，无宽度
            *out_obj_size = 0;
            break;
        }

        case ELEMENT_TYPE_VOID:
        {
            // 语义：无类型，用于方法返回或占位，宽度不定
            // 如果有附加tokens，则第一个字节表示宽度
            if (desc_len > 1)
            {
                *out_obj_size = desc[1];
            }
            else
            {
                return ZC_INTERNAL_TYPE_ILLEGAL_DESC;
            }
            break;
        }

        case ELEMENT_TYPE_BOOLEAN:
        {
            // 语义：布尔值，宽度1B
            *out_obj_size = 1;
            break;
        }

        case ELEMENT_TYPE_CHAR:
        {
            // 语义：Unicode 字符（UTF-16LE），宽度2B
            *out_obj_size = 2;
            break;
        }

        case ELEMENT_TYPE_I1:
        case ELEMENT_TYPE_U1:
        {
            // 语义：8位整数，宽度1B
            *out_obj_size = 1;
            break;
        }

        case ELEMENT_TYPE_I2:
        case ELEMENT_TYPE_U2:
        {
            // 语义：16位整数，宽度2B
            *out_obj_size = 2;
            break;
        }

        case ELEMENT_TYPE_I4:
        case ELEMENT_TYPE_U4:
        {
            // 语义：32位整数，宽度4B
            *out_obj_size = 4;
            break;
        }

        case ELEMENT_TYPE_I8:
        case ELEMENT_TYPE_U8:
        {
            // 语义：64位整数，宽度8B
            *out_obj_size = 8;
            break;
        }

        case ELEMENT_TYPE_R4:
        {
            // 语义：短浮点数，宽度不大于4B
            *out_obj_size = 4;
            break;
        }

        case ELEMENT_TYPE_R8:
        {
            // 语义：长浮点数，宽度不小于4B
            *out_obj_size = 8;
            break;
        }

        case ELEMENT_TYPE_STRING:
        {
            // 语义：不可变 Unicode 字符串（UTF-16LE），宽度不定
            // 附加tokens：4B 字符数量（Length）
            if (desc_len >= 5)
            {
                uint32_t length = *((uint32_t*)(desc + 1));
                *out_obj_size = length * 2; // UTF-16LE每个字符2字节
            }
            else
            {
                return ZC_INTERNAL_TYPE_ILLEGAL_DESC;
            }
            break;
        }

        case ELEMENT_TYPE_PTR:
        {
            // 语义：指向内存块内其他位置的偏移指针，宽度固定8B
            *out_obj_size = 8;
            break;
        }

        case ELEMENT_TYPE_BYREF:
        {
            // 语义：指向内存块内某个有类型变量起始偏移的偏移指针，宽度固定8B
            // 附加tokens：1B 目标数据的类型描述符 + 8B 目标数据的字节长度
            if (desc_len >= 10)
            {
                *out_obj_size = *((uint64_t*)(desc + 2)); // 跳过tag(1B)和类型描述符(1B)
            }
            else
            {
                return ZC_INTERNAL_TYPE_ILLEGAL_DESC;
            }
            break;
        }

        case ELEMENT_TYPE_VALUETYPE:
        {
            // 语义：固定布局的结构体，字段紧密排列，宽度不定
            // 附加tokens：1B 对齐宽度 + 2B 字段数量N + N × [TypeDescriptor]
            // 由于是递归结构，需要解析所有字段才能确定大小
            if (desc_len >= 5)
            {
                uint8_t struct_alignment = desc[1];  // 结构体指定的对齐宽度 (0表示packed)
                uint16_t field_count = *((uint16_t*)(desc + 2)); // 字段数量
                uint64_t current_offset = 4; // tag(1) + alignment(1) + field_count(2)

                // 如果字段数量为0，返回大小1（空结构体大小为1）
                if (field_count == 0)
                {
                    *out_obj_size = 1;
                    break;
                }
                
                // 分配空间存储字段大小
                uint64_t* field_sizes = (uint64_t*)malloc(field_count * sizeof(uint64_t));
                if (!field_sizes) return ZC_INTERNAL_RUN_NOT_INITIALIZED;

                // 提取所有字段的大小
                for (uint16_t i = 0; i < field_count; i++)
                {
                    // 确定字段的描述符长度
                    uint64_t field_len;
                    uint64_t field_size;
                    int32_t flag = zc_get_type_descriptor_length(desc + current_offset, &field_len);
                    if (flag != ZC_INTERNAL_OK)
                    {
                        free(field_sizes);
                        return flag;
                    }

                    flag = zc_type_desc_get_size(desc + current_offset, field_len, &field_size);
                    if (flag != ZC_INTERNAL_OK)
                    {
                        free(field_sizes);
                        return flag;
                    }

                    field_sizes[i] = field_size;
                    current_offset += field_len;
                }
                
                // 如果是 packed 模式
                if (struct_alignment == 0)
                {
                    uint64_t total = 0;
                    for (uint16_t i = 0; i < field_count; i++)
                    {
                        total += field_sizes[i];
                    }
                    free(field_sizes);
                    *out_obj_size = total;
                    break;
                }
                
                // 检查struct_alignment是否为2的幂或者为0
                if ((struct_alignment > 0) && ((struct_alignment & (struct_alignment - 1)) != 0))
                {
                    free(field_sizes);
                    return ZC_INTERNAL_PARAM_ERROR;
                }

                uint64_t offset = 0; // 当前偏移

                for (uint16_t i = 0; i < field_count; i++)
                {
                    uint64_t field_size = field_sizes[i];
                    // 字段对齐要求：不能超过结构体对齐，也不能超过字段自身大小（我们假设字段自然对齐=size）
                    uint64_t field_align = (field_size < struct_alignment) ? field_size : struct_alignment;

                    // 计算为了对齐到 field_align 需要填充多少字节
                    uint64_t padding = (field_align - (offset % field_align)) % field_align;
                    offset += padding;          // 加上填充
                    offset += field_size;       // 加上字段本身
                }

                // 最后整体结构体大小要对齐到 struct_alignment
                uint64_t final_padding = (struct_alignment - (offset % struct_alignment)) % struct_alignment;
                offset += final_padding;

                free(field_sizes);
                *out_obj_size = offset;
            }
            else
            {
                return ZC_INTERNAL_TYPE_ILLEGAL_DESC;
            }

            break;
        }
            
        case ELEMENT_TYPE_CLASS:
        {
            // 语义：动态对象，支持复杂内联嵌套，宽度动态
            // 附加tokens包含内联字段数量 N、Header 大小、header 本体和 N 个字段信息
            // 每个字段信息中包含一个完整的类型描述符、8B 的偏移量、8B 的字段大小
            if (desc_len >= 4)
            {
                uint8_t field_count = desc[1];     // 内联字段数量 N
                uint8_t header_size = desc[2];     // Header 大小 w
                uint64_t offset = 3 + header_size;   // 跳过 tag(1) + N(1) + w(1) + header(w)
                uint64_t total_size = 0;
                
                // 遍历所有字段信息
                for (uint8_t i = 0; i < field_count; i++)
                {
                    // 需要跳过字段类型描述符（长度不定）
                    uint64_t field_type_len;
                    int32_t flag = zc_get_type_descriptor_length(desc + offset, &field_type_len);
                    if (flag != ZC_INTERNAL_OK) return flag;
                    
                    offset += field_type_len;      // 跳过字段类型描述符
                    
                    // 跳过 8B 的字段偏移量
                    offset += 8;
                    
                    // 读取 8B 的字段大小
                    if (offset + 8 <= desc_len)
                    {
                        uint64_t field_size = *((uint64_t*)(desc + offset));
                        total_size += field_size;
                        offset += 8;
                    }
                    else
                    {
                        return ZC_INTERNAL_TYPE_ILLEGAL_DESC;
                    }
                }
                
                *out_obj_size = total_size;
            }
            else
            {
                return ZC_INTERNAL_TYPE_ILLEGAL_DESC;
            }
            break;
        }
            
        case ELEMENT_TYPE_VAR:
        {
            // 语义：特指 CorElementType 枚举类型本身，宽度1B
            *out_obj_size = 1;
            break;
        }
            
        case ELEMENT_TYPE_ARRAY:
        {
            // 语义：多维、非零基数组，宽度动态
            // 附加tokens包含 1B 维度数量 R、一个完整的类型描述符和 R 个维度信息
            // 每个维度信息包括 8B 的维度下界和 8B 的维度长度
            if (desc_len >= 3)
            {
                uint8_t dimensions = desc[1];  // 维度数量 R
                uint64_t offset = 2;             // 当前偏移量，从第3个字节开始
                
                // 获取元素类型描述符的长度
                uint64_t elem_desc_len;
                zc_internal_result_t result = zc_get_type_descriptor_length(desc + offset, &elem_desc_len);
                if (result != ZC_INTERNAL_OK) return result;

                // 获取单个元素宽度
                uint64_t single_elem_size = 0;
                result = zc_type_desc_get_size(desc + offset, elem_desc_len, &single_elem_size);
                if (result != ZC_INTERNAL_OK) return result;

                // 移动偏移到维度信息开始位置
                offset += elem_desc_len;
                
                // 验证缓冲区长度是否足够包含所有维度信息
                // 每个维度需要 8B 下界 + 8B 长度 = 16B
                if (desc_len < offset + dimensions * 16) return ZC_INTERNAL_TYPE_ILLEGAL_DESC;
                
                // 计算数组总大小
                uint64_t total_count = 1;  // 初始化为1，然后累乘各维度长度
                for (uint8_t i = 0; i < dimensions; i++)
                {
                    // 从 desc[offset + i*16 + 8] 读取维度长度 (跳过8字节的下界)
                    uint64_t* dimension_length = (uint64_t*)(desc + offset + i * 16 + 8);
                    total_count *= *dimension_length;
                }
                
                // 数组总大小 = 元素大小 * 所有维度元素总量的乘积
                *out_obj_size = single_elem_size * total_count;
            }
            else
            {
                return ZC_INTERNAL_TYPE_ILLEGAL_DESC; // 缺少必要的附加tokens
            }
            
            break;
        }
            
        case ELEMENT_TYPE_I:
        case ELEMENT_TYPE_U:
        {
            // 语义：平台原生长整型，宽度由平台决定
            // 附加tokens：1B 实际宽度（字节）
            if (desc_len >= 2)
            {
                *out_obj_size = desc[1];
            }
            else
            {
                return ZC_INTERNAL_TYPE_ILLEGAL_DESC; // 缺少必要的附加tokens
            }
            break;
        }
            
        case ELEMENT_TYPE_OBJECT:
        {
            // 语义：未指定类型（泛型）
            // 附加tokens：8B 数据宽度（字节）
            if (desc_len >= 9)
            {
                *out_obj_size = *((uint64_t*)(desc + 1));
            }
            else
            {
                return ZC_INTERNAL_TYPE_ILLEGAL_DESC; // 缺少必要的附加tokens
            }
            break;
        }
            
        case ELEMENT_TYPE_SZARRAY:
        {
            // 语义：单维度零基数组，宽度动态
            // 获取元素类型描述符的长度
            uint64_t elem_desc_len;
            zc_internal_result_t result = zc_get_type_descriptor_length(desc + 1, &elem_desc_len);
            if (result != ZC_INTERNAL_OK) return result;

            // 获取单个元素宽度
            uint64_t single_elem_size = 0;
            result = zc_type_desc_get_size(desc + 1, elem_desc_len, &single_elem_size);
            if (result != ZC_INTERNAL_OK) return result;

            uint32_t total_count = *(uint32_t*)(desc + 1 + elem_desc_len);

            // 数组总大小 = 元素大小 * 元素总量的乘积
            *out_obj_size = single_elem_size * total_count;
            break;
        }
            
        case ELEMENT_TYPE_INTERNAL:
        {
            // 语义：临时变量标记，其后紧跟所标记的变量类型描述符
            // 本身无宽度，需要解析后续的类型描述符
            *out_obj_size = 0;
            break;
        }
            
        default:
        {
            return ZC_INTERNAL_TYPE_ILLEGAL_DESC; // 未知的类型标签
        }
    }
    
    return ZC_INTERNAL_OK;
}