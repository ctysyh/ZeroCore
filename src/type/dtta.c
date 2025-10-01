#include "dtta.h"
#include "type_descriptor.h"
#include <string.h>

/**
 * 添加一个新变量的 dtt 数据
 */
zc_internal_result_t zc_dtt_add(zc_block_header_t* block,
    uint64_t data_offset, uint64_t obj_width, const uint8_t* type_desc, uint64_t desc_len)
{
    // Get LUT pointers
    zc_dtt_lut_header_t* lut_hdr = zc_block_offset_to_ptr(block, ZC_BLOCK_HEADER_SIZE + block->lut_offset);
    if (unlikely(!lut_hdr)) return ZC_INTERNAL_BLOCK_ERROR;
    zc_dtt_lut_entry_t* entries = zc_block_offset_to_ptr(block, lut_hdr->lut_first_entry_offset);
    if (unlikely(!entries)) return ZC_INTERNAL_BLOCK_ERROR;

    // Check LUT entry count
    if (lut_hdr->entry_count >= ZC_DTT_LUT_ENTRY_MAX_COUNT) return ZC_INTERNAL_DTTA_LUT_FULL;

    // Binary search for insert position
    int lo = 0;
    int hi = lut_hdr->entry_count;
    while (lo < hi)
    {
        int mid = (lo + hi) / 2;
        if (entries[mid].data_offset < data_offset)
            lo = mid + 1;
        else
            hi = mid;
    }
    int insert_pos = lo;

    // Check overlap with prev
    if (insert_pos > 0)
    {
        zc_dtt_lut_entry_t* prev = &entries[insert_pos - 1];
        uint64_t prev_width;
        uint8_t* prev_desc = zc_block_offset_to_ptr(block, prev->desc_offset);
        if (!prev_desc) return ZC_INTERNAL_RUN_PTRNULL;

        zc_internal_result_t res = zc_type_desc_get_obj_size(prev_desc, prev->desc_length, &prev_width);
        if (unlikely(res != ZC_INTERNAL_OK)) return res;
        if (prev->data_offset + prev_width > data_offset)
        {
            return ZC_INTERNAL_DTTA_DATA_CONFLICT;
        }
    }

    // Check overlap with next
    if (insert_pos < lut_hdr->entry_count)
    {
        zc_dtt_lut_entry_t* next = &entries[insert_pos];
        if (data_offset + obj_width > next->data_offset)
        {
            return ZC_INTERNAL_DTTA_DATA_CONFLICT;
        }
    }

    // Check DTTA descriptor pool space
    uint64_t current_end = lut_hdr->descriptor_start_offset + lut_hdr->descriptor_length;
    if (lut_hdr->descriptor_length + desc_len > block->cover_page_count * ZC_PAGE_DATA_SIZE)
    {
        return ZC_INTERNAL_DTTA_OVERFLOW;
    }

    // Insert LUT entry
    if (insert_pos < lut_hdr->entry_count)
    {
        memmove(&entries[insert_pos + 1], &entries[insert_pos],
                (lut_hdr->entry_count - insert_pos) * ZC_DTT_LUT_ENTRY_SIZE);
    }
    entries[insert_pos].data_offset = data_offset;
    entries[insert_pos].desc_offset = current_end;
    entries[insert_pos].desc_length = desc_len;

    // Write descriptor
    void* desc_ptr = zc_block_offset_to_ptr(block, current_end);
    if (unlikely(!desc_ptr)) return ZC_INTERNAL_BLOCK_ERROR;
    memcpy(desc_ptr, type_desc, desc_len);

    // Update metadata
    lut_hdr->entry_count++;
    lut_hdr->descriptor_length += desc_len;

    return ZC_INTERNAL_OK;
}

zc_internal_result_t zc_dtt_modify(zc_block_header_t* block,
    uint64_t data_offset, uint64_t new_data_offset, uint64_t new_obj_width,
    const uint8_t* new_type_desc, uint64_t new_desc_len)
{
    // Get LUT pointers
    zc_dtt_lut_header_t* lut_hdr = zc_block_offset_to_ptr(block, ZC_BLOCK_HEADER_SIZE + block->lut_offset);
    if (unlikely(!lut_hdr)) return ZC_INTERNAL_BLOCK_ERROR;
    zc_dtt_lut_entry_t* entries = zc_block_offset_to_ptr(block, lut_hdr->lut_first_entry_offset);
    if (unlikely(!entries)) return ZC_INTERNAL_BLOCK_ERROR;

    // Binary search for old entry
    int lo = 0;
    int hi = lut_hdr->entry_count;
    while (lo < hi)
    {
        int mid = (lo + hi) / 2;
        if (entries[mid].data_offset < data_offset)
            lo = mid + 1;
        else
            hi = mid;
    }
    if (lo >= lut_hdr->entry_count || entries[lo].data_offset != data_offset) return ZC_INTERNAL_DTTA_ENTRY_NOT_FOUND;

    int old_index = lo;
    zc_dtt_lut_entry_t* old_entry = &entries[old_index];

    // Check basic type and desc length
    uint8_t* old_desc = zc_block_offset_to_ptr(block, old_entry->desc_offset);
    if (unlikely(!old_desc)) return ZC_INTERNAL_RUN_PTRNULL;

    if (new_desc_len != old_entry->desc_length || new_type_desc[0] != old_desc[0]) return ZC_INTERNAL_DTTA_DESC_MISMATCH;

    // Check overlap
    int insert_pos = old_index;
    uint64_t new_end = new_data_offset + new_obj_width;
    uint64_t old_obj_width;
    zc_internal_result_t res = zc_type_desc_get_obj_size(old_desc, old_entry->desc_length, &old_obj_width);
    if (unlikely(res != ZC_INTERNAL_OK)) return res;

    if (new_data_offset != data_offset)
    {
        // data_offset changes, search for new insert position
        lo = 0;
        hi = lut_hdr->entry_count;
        while (lo < hi)
        {
            int mid = (lo + hi) / 2;
            if (entries[mid].data_offset < new_data_offset)
                lo = mid + 1;
            else
                hi = mid;
        }
        insert_pos = lo;

        // Check overlap with prev
        if (insert_pos > 0)
        {
            zc_dtt_lut_entry_t* prev = &entries[insert_pos - 1];
            if (prev != old_entry)
            {
                uint64_t prev_width;
                uint8_t* prev_desc_ptr = zc_block_offset_to_ptr(block, prev->desc_offset);
                if (unlikely(!prev_desc_ptr)) return ZC_INTERNAL_RUN_PTRNULL;
                res = zc_type_desc_get_obj_size(prev_desc_ptr, prev->desc_length, &prev_width);
                if (unlikely(res != ZC_INTERNAL_OK)) return res;
                if (prev->data_offset + prev_width > new_data_offset) return ZC_INTERNAL_DTTA_DATA_CONFLICT;
            }
        }

        // Check overlap with next
        if (insert_pos < lut_hdr->entry_count)
        {
            zc_dtt_lut_entry_t* next = &entries[insert_pos];
            if (next != old_entry)
            {
                if (new_end > next->data_offset) return ZC_INTERNAL_DTTA_DATA_CONFLICT;
            }
        }
    }
    else
    {
        // data_offset doesn't change, stay at the same LUT index
        // Check overlap with prev
        if (old_index > 0)
        {
            zc_dtt_lut_entry_t* prev = &entries[old_index - 1];
            uint64_t prev_width;
            uint8_t* prev_desc_ptr = zc_block_offset_to_ptr(block, prev->desc_offset);
            if (!prev_desc_ptr) return ZC_INTERNAL_RUN_PTRNULL;
            res = zc_type_desc_get_obj_size(prev_desc_ptr, prev->desc_length, &prev_width);
            if (unlikely(res != ZC_INTERNAL_OK)) return res;
            if (prev->data_offset + prev_width > new_data_offset) return ZC_INTERNAL_DTTA_DATA_CONFLICT;
        }

        // Check overlap with next
        if (old_index + 1 < lut_hdr->entry_count)
        {
            zc_dtt_lut_entry_t* next = &entries[old_index + 1];
            if (new_end > next->data_offset) return ZC_INTERNAL_DTTA_DATA_CONFLICT;
        }
    }

    // Apply modifications
    memcpy(old_desc, new_type_desc, new_desc_len);

    // If data_offset changes, reorder entries
    if (insert_pos > old_index)
    {
        zc_dtt_lut_entry_t buffer = entries[old_index];
        buffer.data_offset = new_data_offset;

        memmove(&entries[old_index], &entries[old_index + 1], (insert_pos - old_index - 1) * ZC_DTT_LUT_ENTRY_SIZE);

        entries[insert_pos - 1] = buffer;
    }
    else if (insert_pos < old_index)
    {
        zc_dtt_lut_entry_t buffer = entries[old_index];
        buffer.data_offset = new_data_offset;

        memmove(&entries[insert_pos + 1], &entries[insert_pos], (old_index - insert_pos) * ZC_DTT_LUT_ENTRY_SIZE);

        entries[insert_pos] = buffer;
    }
    // if insert_pos == old_index, nothing to do (should not happen if data_offset changed)

    return ZC_INTERNAL_OK;
}

zc_internal_result_t zc_dtt_get_desc_by_data_offset(zc_block_header_t* block,
    uint64_t data_offset, uint8_t** out_type_desc,
    uint64_t* out_desc_len, uint64_t* out_obj_offset)
{
    if (data_offset >= block->lut_offset) return ZC_INTERNAL_BLOCK_ILLEGAL_OFFSET;

    // Get LUT pointers
    zc_dtt_lut_header_t* lut_hdr = zc_block_offset_to_ptr(block, ZC_BLOCK_HEADER_SIZE + block->lut_offset);
    if (unlikely(!lut_hdr)) return ZC_INTERNAL_BLOCK_ERROR;
    zc_dtt_lut_entry_t* entries = zc_block_offset_to_ptr(block, lut_hdr->lut_first_entry_offset);
    if (unlikely(!entries)) return ZC_INTERNAL_BLOCK_ERROR;

    uint32_t entry_count = lut_hdr->entry_count;

    // Binary search for last entry with data_offset <= query_offset
    int lo = 0;
    int hi = entry_count;
    while (lo < hi)
    {
        int mid = (lo + hi) / 2;
        if (entries[mid].data_offset <= data_offset)
        {
            lo = mid + 1;
        }
        else
        {
            hi = mid;
        }
    }
    int candidate_index = lo - 1;

    // Case 1: candidate exists, check if covering query_offset
    if (candidate_index >= 0)
    {
        zc_dtt_lut_entry_t* candidate = &entries[candidate_index];
        uint8_t* desc_ptr = (uint8_t*)zc_block_offset_to_ptr(block, candidate->desc_offset);
        if (unlikely(!desc_ptr)) return ZC_INTERNAL_RUN_PTRNULL;

        uint64_t obj_width = 0;
        zc_internal_result_t res = zc_type_desc_get_obj_size(desc_ptr, candidate->desc_length, &obj_width);
        if (unlikely(res != ZC_INTERNAL_OK)) return res;

        uint64_t obj_end = candidate->data_offset + obj_width;
        if (data_offset < obj_end)
        {
            // Offset hits candidate
            *out_obj_offset = candidate->data_offset;
            *out_desc_len = candidate->desc_length;
            *out_type_desc = desc_ptr;
            return ZC_INTERNAL_OK;
        }
        // Else, offset is at the hole after candidate
        *out_obj_offset = obj_end;
    }
    // Case 2: candidate doesn't exist, meaning offset is before the first variable
    else
    {
        *out_obj_offset = 0;
    }

    *out_type_desc = NULL;
    *out_desc_len = 0;

    return ZC_INTERNAL_OK;
}

zc_internal_result_t zc_dtt_get_desc_by_ptr_offset(zc_block_header_t* block,
    uint64_t ptr_offset, uint8_t** out_target_type_desc, uint64_t* out_target_desc_len,
    uint64_t* out_target_obj_offset, uint64_t* out_target_obj_size)
{
    // Get ptr variable at ptr_offset
    uint8_t* ptr_desc = NULL;
    uint64_t ptr_desc_len = 0;
    uint64_t ptr_obj_offset = 0;
    zc_internal_result_t res = zc_dtt_get_desc_by_data_offset(
        block, ptr_offset, &ptr_desc, &ptr_desc_len, &ptr_obj_offset);
    if (unlikely(res != ZC_INTERNAL_OK)) return res;

    if (ptr_desc == NULL || ptr_desc[0] != 0x0F || ptr_desc_len != 9) return ZC_INTERNAL_TYPE_ILLEGAL_DESC;
    if (ptr_obj_offset != ptr_offset) return ZC_INTERNAL_TYPE_ILLEGAL_PTR;

    // Get target_size from ptr_desc (little-endian order)
    uint64_t target_size = 0;
    memcpy(&target_size, &ptr_desc[1], sizeof(uint64_t));

    // Analysis target_offset
    uint64_t* ptr_content = (uint64_t*)zc_block_offset_to_ptr(block, ptr_offset);
    if (unlikely(!ptr_content)) return ZC_INTERNAL_RUN_PTRNULL;
    uint64_t target_offset = *ptr_content;

    uint8_t* target_desc = NULL;
    uint64_t target_desc_len = 0;
    uint64_t target_obj_offset = 0;
    res = zc_dtt_get_desc_by_data_offset(
        block, target_offset, &target_desc, &target_desc_len, &target_obj_offset);
    if (unlikely(res != ZC_INTERNAL_OK)) return res;

    if (target_desc == NULL) return ZC_INTERNAL_TYPE_ILLEGAL_PTR;

    // Verify the legitimacy of the scope
    uint64_t target_obj_size = 0;
    res = zc_type_desc_get_obj_size(target_desc, target_desc_len, &target_obj_size);
    if (unlikely(res != ZC_INTERNAL_OK)) return res;

    if (target_offset + target_size > target_obj_offset + target_obj_size)
    {
        return ZC_INTERNAL_TYPE_ILLEGAL_PTR;
    }

    *out_target_type_desc = target_desc;
    *out_target_desc_len = target_desc_len;
    *out_target_obj_offset = target_obj_offset;
    *out_target_obj_size = target_obj_size;

    return ZC_INTERNAL_OK;
}

zc_internal_result_t zc_dtt_get_desc_by_byref_offset(zc_block_header_t* block,
    uint64_t byref_offset, uint8_t** out_target_type_desc, uint64_t* out_target_desc_len,
    uint64_t* out_target_obj_offset, uint64_t* out_target_obj_size)
{
    // Get BYREF variable meta data
    uint8_t* byref_desc = NULL;
    uint64_t byref_desc_len = 0;
    uint64_t byref_obj_offset = 0;
    zc_internal_result_t res = zc_dtt_get_desc_by_data_offset(
        block, byref_offset, &byref_desc, &byref_desc_len, &byref_obj_offset);
    if (unlikely(res != ZC_INTERNAL_OK)) return res;

    if (byref_desc == NULL || byref_desc[0] != 0x10 || byref_desc_len != 10) return ZC_INTERNAL_TYPE_ILLEGAL_DESC;
    if (byref_obj_offset != byref_offset) return ZC_INTERNAL_TYPE_ILLEGAL_BYREF;

    uint8_t target_type_tag = byref_desc[1];
    uint64_t target_size = 0;
    memcpy(&target_size, &byref_desc[2], sizeof(uint64_t));

    // Read target_offset
    uint64_t* byref_content = (uint64_t*)zc_block_offset_to_ptr(block, byref_offset);
    if (unlikely(!byref_content))return ZC_INTERNAL_RUN_PTRNULL;
    uint64_t target_offset = *byref_content;

    // Analysis target_offset
    uint8_t* target_desc = NULL;
    uint64_t target_desc_len = 0;
    uint64_t target_obj_offset = 0;
    res = zc_dtt_get_desc_by_data_offset(
        block, target_offset, &target_desc, &target_desc_len, &target_obj_offset);
    if (unlikely(res != ZC_INTERNAL_OK)) return res;

    if (target_desc == NULL || target_desc[0] != target_type_tag) return ZC_INTERNAL_TYPE_ILLEGAL_BYREF;

    // Verify the legitimacy and integrity of the reference scope
    uint64_t current = target_offset;
    uint64_t range_end = target_offset + target_size;
    while (current < range_end)
    {
        uint64_t next_offset = 0;
        res = zc_dtt_get_next_sibling_offset(block, current, &next_offset);
        if (unlikely(res != ZC_INTERNAL_OK)) return res;

        if (next_offset == 0 || next_offset > range_end) return ZC_INTERNAL_TYPE_ILLEGAL_BYREF;

        if (next_offset == range_end) break;

        current = next_offset;
    }

    uint64_t target_obj_size = 0;
    res = zc_type_desc_get_obj_size(target_desc, target_desc_len, &target_obj_size);
    if (unlikely(res != ZC_INTERNAL_OK)) return res;

    *out_target_type_desc = target_desc;
    *out_target_desc_len = target_desc_len;
    *out_target_obj_offset = target_obj_offset;
    *out_target_obj_size = target_obj_size;

    return ZC_INTERNAL_OK;
}

