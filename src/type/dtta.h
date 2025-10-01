/*
*/
#pragma once

#ifndef DTTA_H
#define DTTA_H

#ifdef __cplusplus
extern "C" {
#endif

#include "block.h"
#include "zerocore_internal.h"

typedef struct zc_dtt_lut_entry
{
    uint64_t data_offset;             // UserData 变量的起始偏移 (相对于块首)
    uint64_t desc_offset;             // 类型描述符的起始偏移
    uint64_t desc_length;             // 类型描述符长度
} zc_dtt_lut_entry_t;

#ifndef ZC_DTT_LUT_ENTRY_SIZE
#define ZC_DTT_LUT_ENTRY_SIZE sizeof(zc_dtt_lut_entry_t)
#endif

typedef struct zc_dtt_lut_header
{
    uint32_t entry_count;             // LUT 项目数量
    uint64_t lut_first_entry_offset;  // LUT 首项的起始偏移 (相对于块首)
    uint64_t descriptor_start_offset; // 描述符字节流的起始偏移 (相对于块首)
    uint64_t descriptor_length;       // 描述符字节流的长度
} zc_dtt_lut_header_t;

#ifndef ZC_DTT_LUT_HEADER_SIZE
#define ZC_DTT_LUT_HEADER_SIZE sizeof(zc_dtt_lut_entry_t)
#endif
#ifndef ZC_DTT_LUT_ENTRY_MAX_COUNT
#define ZC_DTT_LUT_ENTRY_MAX_COUNT 16
#endif

/**
 * @brief 在块的 DTTA 中新增一个变量的类型描述条目（LUT + 描述符）。
 *
 * 此函数为内部工具函数，仅用于在外部接口新增变量时注册其类型信息。
 * 它严格要求新变量的内存区间 `[data_offset, data_offset + width)` 
 * 与所有现有变量（无论有类型或无类型）完全不重叠。若需扩展已有无类型变量
 * 或修改现有变量，应调用 zc_dtt_modify()。
 *
 * @param block        [in] 已 acquire 的块头指针，状态必须为 FREE。
 * @param data_offset  [in] 新变量在用户数据区内的起始偏移（字节）。
 * @param type_desc    [in] 指向类型描述符的内存地址（非块内偏移）。
 * @param desc_len     [in] 类型描述符长度（字节），必须 > 0。
 *
 * @return
 * - ZC_INTERNAL_OK: 成功添加条目。
 * - ZC_INTERNAL_INVALID_TYPE_DESC: type_desc 无法解析或宽度为 0。
 * - ZC_INTERNAL_DTTA_LUT_FULL: LUT 条目已达上限（16 条）。
 * - ZC_INTERNAL_DTTA_TYPE_CONFLICT: 新变量与现有变量内存区间重叠。
 * - ZC_INTERNAL_DTTA_OVERFLOW: DTTA 描述符空间不足。
 * - ZC_INTERNAL_BLOCK_ERROR: 块结构损坏（如偏移转换失败）。
 * - ZC_INTERNAL_RUN_PTRNULL: 内部指针为空（严重错误，通常不应发生）。
 *
 * @note
 * - 函数不检查参数的指针有效性。
 * - 调用前必须确保调用者拥有块。
 * - 不合并无类型变量；即使两个无类型变量重叠，也视为冲突。
 * - 描述符将追加到 DTTA 描述符池末尾，LUT 条目按 data_offset 升序插入。
 * - 此函数不移动用户数据，仅更新元数据。
 */
zc_internal_result_t zc_dtt_add(
    zc_block_header_t* block,
    uint64_t data_offset,
    uint64_t obj_width,
    const uint8_t* type_desc,
    uint64_t desc_len
);

/**
 * @brief 修改块中已存在变量的类型元数据（LUT 条目 + 描述符）。
 *
 * 此函数用于在写入者持有 FREE 状态块期间，修改已有变量的起始偏移、宽度或描述符内容。
 * 要求新描述符长度与原描述符严格一致，且基本类型（首字节）不变。支持 data_offset 变更，
 * 此时会自动重排 LUT 以维持升序。描述符在池中 in-place 更新，不产生空洞。
 *
 * @param block           [in] 已 acquire 的块头指针，状态必须为 FREE。
 * @param data_offset     [in] 原变量在用户数据区的起始偏移（用于定位 LUT 条目）。
 * @param new_data_offset [in] 新的起始偏移（可与原值相同）。
 * @param new_obj_width   [in] 新的对象字节宽度（用于重叠检查）。
 * @param new_type_desc   [in] 指向新类型描述符的内存地址（必须非 NULL）。
 * @param new_desc_len    [in] 新描述符长度（字节），必须等于原描述符长度。
 *
 * @return
 * - ZC_INTERNAL_OK: 成功修改元数据。
 * - ZC_INTERNAL_BLOCK_ERROR: 块结构损坏（LUT 头或条目指针无效）。
 * - ZC_INTERNAL_RUN_PTRNULL: 内部指针转换失败（严重错误）。
 * - ZC_INTERNAL_DTTA_ENTRY_NOT_FOUND: 未找到 data_offset 对应的 LUT 条目。
 * - ZC_INTERNAL_DTTA_DESC_MISMATCH: 描述符长度不匹配或基本类型改变。
 * - ZC_INTERNAL_DTTA_DATA_CONFLICT: 新数据区间与现有变量（含无类型）重叠。
 *
 * @note
 * - 函数不检查参数的指针有效性。
 * - 调用前必须确保调用者拥有块。
 * - 不移动用户数据，仅更新 DTTA 元数据。
 * - 描述符池保持紧凑，禁止 desc_len 变化。
 * - LUT 始终按 data_offset 升序维护，data_offset 变更时自动重排序。
 */
zc_internal_result_t zc_dtt_modify(
    zc_block_header_t* block,
    uint64_t data_offset,
    uint64_t new_data_offset,
    uint64_t new_obj_width,
    const uint8_t* new_type_desc,
    uint64_t new_desc_len
);

/**
 * @brief 查询用户数据区任意偏移所属的变量元数据。
 *
 * 此函数根据给定的用户数据区偏移 `data_offset`，返回其所属变量的类型描述符、长度及该变量对象的起始偏移。
 * 若偏移落在已注册变量（含 0x5C RAWBITS）范围内，则返回对应元数据；
 * 若落在未被任何变量覆盖的“空洞”区域，则返回 NULL 描述符、长度 0，并将对象起始偏移设为该空洞的起始位置。
 * 该函数是所有基于偏移的类型查询（如 Load、PTR/BYREF 解析）的基础。
 *
 * @param block           [in] 已 acquire 的块头指针。
 * @param data_offset     [in] 用户数据区内的任意字节偏移（必须在 [0, user_data_size) 范围内）。
 * @param out_type_desc   [out] 指向块内类型描述符的虚拟地址；若为未注册区域则为 NULL。
 * @param out_desc_len    [out] 类型描述符的字节长度；若为未注册区域则为 0。
 * @param out_obj_offset  [out] 所属对象（或空洞段）在用户数据区的起始偏移。
 *
 * @return
 * - ZC_INTERNAL_OK: 查询成功。落在未注册区域也视为成功。
 * - ZC_INTERNAL_INVALID_ARG: 输入参数为 NULL。
 * - ZC_INTERNAL_OUT_OF_BOUNDS: data_offset 超出用户数据区范围。
 * - ZC_INTERNAL_BLOCK_ERROR: 块结构损坏（LUT 头或条目偏移无效）。
 * - ZC_INTERNAL_RUN_PTRNULL: 内部指针转换失败（严重错误）。
 * - ZC_INTERNAL_INVALID_TYPE_DESC: 候选变量的类型描述符无法解析（如损坏或非法格式）。
 *
 * @note
 * - 函数不检查参数的指针有效性。
 * - 该函数为只读操作，读写者均可调用。
 * - 返回的 out_type_desc 是块内指针，其生命周期由块句柄管理，不应被缓存。
 */
zc_internal_result_t zc_dtt_get_desc_by_data_offset(
    zc_block_header_t* block,
    uint64_t data_offset,
    uint8_t** out_type_desc,
    uint64_t* out_desc_len,
    uint64_t* out_obj_offset
);

/**
 * @brief 通过 PTR 变量偏移解析其指向的目标对象元数据。
 *
 * 1. 读取 PTR 变量内容（UserData 中的 uint64_t）获取 target_offset；
 * 2. 从 PTR 描述符（[0x0F, target_size]）获取 target_size；
 * 3. 验证 target_offset 指向一个已注册变量（非空洞）；
 * 4. 验证 [target_offset, target_offset+target_size) 完全包含于目标对象范围内。
 *
 * @param block                 [in] 已 acquire 的块头。
 * @param ptr_offset            [in] PTR 变量在用户数据区的起始偏移。
 * @param out_target_type_desc  [out] 目标对象的类型描述符。
 * @param out_target_desc_len   [out] 目标对象描述符长度。
 * @param out_target_obj_offset [out] 目标对象的起始偏移。
 * @param out_target_obj_size   [out] 目标对象的总字节大小。
 *
 * @return
 * - ZC_INTERNAL_OK: 成功解析。
 * - ZC_INTERNAL_INVALID_TYPE_DESC: 
 *     - ptr_offset 处变量类型非 PTR (0x0F)；
 *     - PTR 描述符长度 ≠ 9 字节。
 * - ZC_INTERNAL_TYPE_ILLEGAL_PTR:
 *     - ptr_offset 不是任何变量的起始偏移；
 *     - target_offset 落在未注册的空洞区域；
 *     - [target_offset, target_offset + target_size) 超出目标对象边界。
 * - ZC_INTERNAL_RUN_PTRNULL: 块结构损坏或内部指针转换失败。
 * - ZC_INTERNAL_DTTA_ENTRY_NOT_FOUND / 其他: 由 zc_dtt_get_desc_by_data_offset 或 zc_type_desc_get_obj_size 透传的错误。
 *
 * @note
 * - 函数不检查参数的指针有效性。
 * - 此函数要求 ptr_offset 必须是已注册的 PTR (0x0F) 类型变量的起始偏移。
 * - 目标区域必须是已注册变量，不允许指向 RAWBITS 空洞。
 */
zc_internal_result_t zc_dtt_get_desc_by_ptr_offset(
    zc_block_header_t* block,
    uint64_t ptr_offset,
    uint8_t** out_target_type_desc,
    uint64_t* out_target_desc_len,
    uint64_t* out_target_obj_offset,
    uint64_t* out_target_obj_size
);

/**
 * @brief 通过 BYREF 变量偏移解析其引用的目标对象元数据，并验证引用完整性。
 *
 * BYREF (0x10) 是强类型引用，要求：
 * 1. target_offset 必须是字段/变量的起始偏移；
 * 2. BYREF 描述符中的 target_type_tag 必须与目标对象主类型码一致；
 * 3. [target_offset, target_offset + target_size) 必须恰好覆盖一个或多个连续的平级字段。
 *
 * @param block                 [in] 已 acquire 的块头。
 * @param byref_offset          [in] BYREF 变量在用户数据区的起始偏移。
 * @param out_target_type_desc  [out] 目标对象的完整类型描述符。
 * @param out_target_desc_len   [out] 目标描述符长度。
 * @param out_target_obj_offset [out] 目标对象（或字段所属父对象）的起始偏移。
 * @param out_target_obj_size   [out] 目标对象的总字节大小。
 *
 * @return
 * - ZC_INTERNAL_OK: 成功解析且引用合法。
 * - ZC_INTERNAL_INVALID_TYPE_DESC: BYREF 描述符格式错误（非 0x10 或长度 ≠ 10）。
 * - ZC_INTERNAL_TYPE_ILLEGAL_BYREF: 
 *     - BYREF 声明的目标类型与实际目标主类型码不匹配;
 *     - byref_offset 不是变量起始；
 *     - target_offset 不是字段/变量起始；
 *     - 引用范围未对齐平级字段边界。
 * - 其他内部错误（如块损坏、描述符解析失败）。
 *
 * @note
 * - 函数不检查参数的指针有效性。
 */
zc_internal_result_t zc_dtt_get_desc_by_byref_offset(
    zc_block_header_t* block,
    uint64_t byref_offset,
    uint8_t** out_target_type_desc,
    uint64_t* out_target_desc_len,
    uint64_t* out_target_obj_offset,
    uint64_t* out_target_obj_size
);

// 给定一个块和一个偏移 offset，若该偏移是某个变量或字段的起始偏移，则传出其结束偏移（即下一个平级起始偏移）；否则传出NULL。
zc_internal_result_t zc_dtt_get_next_sibling_offset(
    zc_block_header_t* block,
    uint64_t offset,
    uint64_t* out_next_offset
);

#ifdef __cplusplus
}
#endif

#endif /* DTTA_H */