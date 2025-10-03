# ZeroCore API Reference Manual

---

## 目录

<!-- TOC -->
- [ZeroCore API Reference Manual](#zerocore-api-reference-manual)
  - [目录](#目录)
  - [一、系统 API](#一系统-api)
  - [二、Zora API](#二zora-api)
    - [2.1 类型描述符](#21-类型描述符)
      - [0x00 — `ELEMENT_TYPE_END`](#0x00--element_type_end)
      - [0x01 — `ELEMENT_TYPE_VOID`](#0x01--element_type_void)
      - [0x02 — `ELEMENT_TYPE_BOOLEAN`](#0x02--element_type_boolean)
      - [0x03 — `ELEMENT_TYPE_CHAR`](#0x03--element_type_char)
      - [0x04 — `ELEMENT_TYPE_I1`](#0x04--element_type_i1)
      - [0x05 — `ELEMENT_TYPE_U1`](#0x05--element_type_u1)
      - [0x06 — `ELEMENT_TYPE_I2`](#0x06--element_type_i2)
      - [0x07 — `ELEMENT_TYPE_U2`](#0x07--element_type_u2)
      - [0x08 — `ELEMENT_TYPE_I4`](#0x08--element_type_i4)
      - [0x09 — `ELEMENT_TYPE_U4`](#0x09--element_type_u4)
      - [0x0A — `ELEMENT_TYPE_I8`](#0x0a--element_type_i8)
      - [0x0B — `ELEMENT_TYPE_U8`](#0x0b--element_type_u8)
      - [0x0C — `ELEMENT_TYPE_R4`](#0x0c--element_type_r4)
      - [0x0D — `ELEMENT_TYPE_R8`](#0x0d--element_type_r8)
      - [0x0E — `ELEMENT_TYPE_STRING`](#0x0e--element_type_string)
      - [0x0F — `ELEMENT_TYPE_PTR`](#0x0f--element_type_ptr)
      - [0x10 — `ELEMENT_TYPE_BYREF`](#0x10--element_type_byref)
      - [0x11 — `ELEMENT_TYPE_VALUETYPE`](#0x11--element_type_valuetype)
      - [0x12 — `ELEMENT_TYPE_CLASS`](#0x12--element_type_class)
      - [0x13 — `ELEMENT_TYPE_VAR`](#0x13--element_type_var)
      - [0x14 — `ELEMENT_TYPE_ARRAY`](#0x14--element_type_array)
      - [0x18 — `ELEMENT_TYPE_I`](#0x18--element_type_i)
      - [0x19 — `ELEMENT_TYPE_U`](#0x19--element_type_u)
      - [0x1C — `ELEMENT_TYPE_OBJECT`](#0x1c--element_type_object)
      - [0x1D — `ELEMENT_TYPE_SZARRAY`](#0x1d--element_type_szarray)
    - [2.2 数据写入 API](#22-数据写入-api)
      - [Store API](#store-api)
      - [Copy API](#copy-api)
      - [Delete API](#delete-api)
    - [2.3 数据读取 API](#23-数据读取-api)
      - [Load API](#load-api)
      - [Info API](#info-api)
      - [Compare API](#compare-api)
    - [2.4 算子 API](#24-算子-api)
  - [三、错误码枚举 `zc_result_t`](#三错误码枚举-zc_result_t)
    - [3.1 枚举定义](#31-枚举定义)

<!-- /TOC -->

---

## 一、系统 API

<TODO> **从ZeroCore 需求文档中迁移过来**

---

## 二、Zora API

### 2.1 类型描述符

- 所有类型描述符由 1 字节 Tag + 附加 tokens 构成。
- 派生符位（高第2位）用于从原类型派生出新类型。
- 修饰符位（高第1位）保留。
- 部分枚举值暂不启用，在下方省略。

#### 0x00 — `ELEMENT_TYPE_END`
- 语义：类型流结束标记
- 宽度：无
- 附加tokens：
  - 无
- 派生：
  - 0x40 — `ELEMENT_TYPE_SEPARATOR`
    - 语义：复杂类型间的分隔符
    - 宽度：无
    - 附加tokens：
      - 无
  - 0x80 — `ELEMENT_TYPE_PREFIX`
    - 语义：类型描述符的起始前缀符
    - 宽度：无
    - 附加tokens：
      - 无

#### 0x01 — `ELEMENT_TYPE_VOID`
- 语义：无类型，用于方法返回或占位
- 宽度：不定
- 附加tokens：
  - `1B` — 表示该空类型占位宽度（字节，2 的指数表示值 +1，为 0 表示确实为 0）

#### 0x02 — `ELEMENT_TYPE_BOOLEAN`
- 语义：布尔值
- 宽度：`1B`
- 附加tokens：
  - 无

#### 0x03 — `ELEMENT_TYPE_CHAR`
- 语义：Unicode 字符（UTF-16LE）
- 宽度：`2B`
- 附加tokens：
  - 无

#### 0x04 — `ELEMENT_TYPE_I1`
- 语义：有符号 8 位整数
- 宽度：`1B`
- 附加tokens：
  - 无
- 派生：
  - 0x44 — `ELEMENT_TYPE_SIGNEDASCII`
    - 语义：执行符号扩展的 ASCII 值
    - 宽度：`1B`
    - 附加tokens：
      - 无

#### 0x05 — `ELEMENT_TYPE_U1`
- 语义：无符号 8 位整数
- 宽度：`1B`
- 附加tokens：
  - 无
- 可用修饰：
  - 0x45 — `ELEMENT_TYPE_ASCII`
    - 语义：标准的/不执行符号扩展的 ASCII 值
    - 宽度：`1B`
    - 附加tokens：
      - 无

#### 0x06 — `ELEMENT_TYPE_I2`
- 语义：有符号 16 位整数
- 宽度：`2B`
- 附加tokens：
  - 无

#### 0x07 — `ELEMENT_TYPE_U2`
- 语义：无符号 16 位整数
- 宽度：`2B`
- 附加tokens：
  - 无

#### 0x08 — `ELEMENT_TYPE_I4`
- 语义：有符号 32 位整数
- 宽度：`4B`
- 附加tokens：
  - 无

#### 0x09 — `ELEMENT_TYPE_U4`
- 语义：无符号 32 位整数
- 宽度：`4B`
- 附加tokens：
  - 无

#### 0x0A — `ELEMENT_TYPE_I8`
- 语义：有符号 64 位整数
- 宽度：`8B`
- 附加tokens：
  - 无

#### 0x0B — `ELEMENT_TYPE_U8`
- 语义：无符号 64 位整数
- 宽度：`8B`
- 附加tokens：
  - 无

#### 0x0C — `ELEMENT_TYPE_R4`
- 语义：短浮点数
- 宽度：不大于 `4B`
- 附加tokens：
  - `1B` — 专属格式 Token（预留，后续定义FP16、FP8、FP4、NVFP4等）
- 派生：
  - 0x4C — `ELEMENT_TYPE_FLOATTENSOR`
    - 语义：浮点张量
    - 宽度：不定
    - 附加tokens：
      - `1B` — 专属格式 Token（预留）
      - `2B` — 张量阶数 `O`
      - `O × 2B` — 每阶维度数量 `D_i`

#### 0x0D — `ELEMENT_TYPE_R8`
- 语义：长浮点数
- 宽度：不小于 `4B`
- 附加tokens：
  - `1B` — 专属格式 Token（预留，后续定义扩展精度等）
- 派生：
  - 0x4D — `ELEMENT_TYPE_FIXEDPOINT`
    - 语义：定点数
    - 宽度：不定
    - 附加tokens：
      - `1B` — 专属格式 Token（预留）

#### 0x0E — `ELEMENT_TYPE_STRING`
- 语义：不可变 Unicode 字符串（UTF-16LE）
- 宽度：不定（由字符数量决定）
- 附加tokens：
  - `4B` — 字符数量
- 派生：
  - 0x4E — `ELEMENT_TYPE_ASCIISTRING`
    - 语义：不可变 ASCII 字符串
    - 宽度：不定（由字符数量决定）
    - 附加tokens：
      - `4B` — 字符数量

#### 0x0F — `ELEMENT_TYPE_PTR`
- 语义：指向内存块内其他位置的偏移指针
- 宽度：`8B`
- 附加tokens：
  - `8B` — 指向的连续内存区域的字节长度

#### 0x10 — `ELEMENT_TYPE_BYREF`
- 语义：指向内存块内某个有类型变量起始偏移的偏移指针
- 宽度：`8B`
- 附加tokens：
  - `1B` — 目标数据的类型描述符（必须是有效 `CorElementType`）
  - `8B` — 目标数据的字节长度

#### 0x11 — `ELEMENT_TYPE_VALUETYPE`
- 语义：固定布局的结构体，允许静态嵌套
- 宽度：不定（由字段布局决定）
- 附加tokens：
  - `4B` — 描述符的附加tokens总长度（字节）
  - `8B` — 对象总宽度（字节）
  - `1B` — 对齐宽度（0=packed）
  - `1B` — 字段数量 `N`
  - `N × [TypeDescriptor]` — 按内存布局顺序排列的字段类型描述符

#### 0x12 — `ELEMENT_TYPE_CLASS`
- 语义：动态对象，支持复杂内联嵌套
- 宽度：动态（允许成员的字段大小改变）
- 附加tokens：
  - `4B` — 描述符的附加tokens总长度（字节）
  - `8B` — 对象总宽度（字节）
  - `4B` — 内联字段数量 `N`（最大 65,535 个字段）
  - `2B` — Header 大小 `w` （字节，0 表示无 Header）
  - `wB` — Header
  - `N × 12B` — 查找表，结构如下：
    - `8B` — 字段偏移量（从 `CLASS` 对象的起始地址算起）
    - `4B` — 字段类型描述符偏移量（从 `CLASS` 的 TypeDescriptor 起始地址算起）
  - `N × [TypeDescriptor]` — 字段类型描述符

#### 0x13 — `ELEMENT_TYPE_VAR`
- 语义：特指 `CorElementType` 枚举类型本身
- 宽度：`1B`
- 附加tokens：
  - 无

#### 0x14 — `ELEMENT_TYPE_ARRAY`
- 语义：多维、非零基数组
- 宽度：动态（由维度和元素数量决定）
- 附加tokens：
  - `[TypeDescriptor]` — 元素类型描述符
  - `1B` — 维度数量 `R`（最大 255）
  - `R ×` 以下结构：
    - `8B` — 维度下界 `LowerBound`
    - `8B` — 维度长度 `Length`

#### 0x18 — `ELEMENT_TYPE_I`
- 语义：平台原生有符号长整型
- 宽度：由写入者平台决定
- 附加tokens：
  - `1B` — 实际宽度（字节，2的指数表示）

#### 0x19 — `ELEMENT_TYPE_U`
- 语义：平台原生无符号长整型或本地指针
- 宽度：由写入者平台决定
- 附加tokens：
  - `1B` — 实际宽度（字节，2的指数表示）

#### 0x1C — `ELEMENT_TYPE_OBJECT`
- 语义：未指定类型（泛型）
- 宽度：不定
- 附加tokens：
  - `8B` — 数据宽度（字节）
- 派生：
  - 0x5C — `ELEMENT_TYPE_RAWBITS`
    - 语义：无类型位流
    - 宽度：不定
    - 附加tokens：
      - `8B` — 数据宽度（字节）

#### 0x1D — `ELEMENT_TYPE_SZARRAY`
- 语义：单维度零基数组
- 宽度：动态（由元素数量和类型决定）
- 附加tokens：
  - `[TypeDescriptor]` — 元素类型描述符
  - `4B` — 元素数量

### 2.2 数据写入 API

#### Store API

```c
ZC_API zc_result_t zc_store(
    zc_writer_id_t writer_id,
    zc_handle_t handle,
    size_t offset,
    const void* value,
    const uint8_t* type_desc,
    size_t desc_len,
    zc_store_action action
);
```
- 写入数据并更新 DTTA。
- 校验权限、边界、类型冲突。
- 支持插入新条目或合并现有条目。

```c
ZC_API zc_result_t zc_store_byptr(
    zc_writer_id_t writer_id,
    zc_handle_t handle,
    size_t ptr_offset,
    const void* value,
    zc_store_action action
);
```
- 通过 DTTA 中类型为 `0x0F (PTR)` 的条目解析目标地址，然后执行写入。

```c
ZC_API zc_result_t zc_store_byref(
    zc_writer_id_t writer_id,
    zc_handle_t handle,
    size_t ref_offset,
    const void* value,
    zc_store_action action
);
```
- 通过 DTTA 中类型为 `0x10 (BYREF)` 的条目解析目标地址和目标类型，然后执行写入。

#### Copy API

```c
ZC_API zc_result_t zc_copy(
    zc_writer_id_t writer_id,
    zc_handle_t handle,
    size_t src_offset,
    size_t dst_offset,
    const uint8_t* type_desc,
    size_t desc_len,
    zc_copy_action action
);
```
- 复制数据，并根据 `action` 更新目标区域的 DTTA 类型。

```c
ZC_API zc_result_t zc_copy_byptr(
    zc_writer_id_t writer_id,
    zc_handle_t handle,
    size_t src_ptr_offset,
    size_t dst_ptr_offset,
    zc_copy_byptr_action action
);
ZC_API zc_result_t zc_copy_byref(
    zc_writer_id_t writer_id,
    zc_handle_t handle,
    size_t src_ref_offset,
    size_t dst_ref_offset,
    zc_copy_byref_action action
);
```
- 通过指针或引用解析源和目标地址，然后执行复制。

#### Delete API

```c
ZC_API zc_result_t zc_delete(
    zc_writer_id_t writer_id,
    zc_handle_t handle,
    size_t offset,
    const uint8_t* type_desc,
    size_t desc_len,
    zc_delete_action action
);
```
- 删除指定偏移量处的变量或区域，更新 DTTA。

```c
ZC_API zc_result_t zc_delete_byptr(
    zc_writer_id_t writer_id,
    zc_handle_t handle,
    size_t ptr_offset,
    zc_delete_action action
);
ZC_API zc_result_t zc_delete_byref(
    zc_writer_id_t writer_id,
    zc_handle_t handle,
    size_t ref_offset,
    zc_delete_action action
);
```
- 通过指针或引用解析目标地址，然后执行删除。

### 2.3 数据读取 API

- <TODO> **本小节的 API 写入者也应该能用，需要为写入者定义一套平行的数据读取 API。**
 
#### Load API

```c
ZC_API zc_result_t zc_load(
    zc_reader_id_t reader_id,
    zc_handle_t handle,
    size_t offset,
    void* out_value,
    const uint8_t* expected_desc,
    size_t expected_desc_len
);
```
- 加载数据，校验 `offset` 处的实际类型与 `expected_desc` 精确匹配。

```c
ZC_API zc_result_t zc_load_byptr(
    zc_reader_id_t reader_id,
    zc_handle_t handle,
    size_t ptr_offset,
    void* out_value
);
ZC_API zc_result_t zc_load_byref(
    zc_reader_id_t reader_id,
    zc_handle_t handle,
    size_t ref_offset,
    void* out_value
    );
```
- 通过指针或引用解析目标地址和类型，校验匹配后加载数据。

#### Info API

```c
ZC_API zc_result_t zc_sizeof(
    zc_reader_id_t reader_id,
    zc_handle_t handle,
    size_t offset,
    size_t* out_size);
ZC_API zc_result_t zc_typeof(
    zc_reader_id_t reader_id,
    zc_handle_t handle,
    size_t offset,
    uint8_t* out_desc,
    size_t* out_desc_len
);
ZC_API zc_result_t zc_typeof_byptr(
    zc_reader_id_t reader_id,
    zc_handle_t handle,
    size_t ptr_offset,
    uint8_t* out_desc,
    size_t* out_desc_len
);
```

#### Compare API

```c
// <TODO> **比较 API 需要增加，涵盖与参数传入的中间变量比较、与块内另一个变量比较、通过指针与参数传入的中间变量比较、通过指针与块内另一个变量比较、通过两个指针比较**
ZC_API zc_result_t zc_is_equal(
    zc_reader_id_t reader_id,
    zc_handle_t handle,
    size_t offset,
    const void* expected,
    size_t size,
    bool* out_result
);
ZC_API zc_result_t zc_is_unequal(
    zc_reader_id_t reader_id,
    zc_handle_t handle,
    size_t offset,
    const void* expected,
    size_t size,
    bool* out_result
);

// 数值比较 API (需为数字类型)
// <TODO> **与前一组比较 API 相同的扩展要求**
ZC_API zc_result_t zc_is_bigger(
    zc_reader_id_t reader_id,
    zc_handle_t handle,
    size_t offset,
    const void* threshold,
    size_t size,
    bool* out_result
);
ZC_API zc_result_t zc_is_smaller(
    zc_reader_id_t reader_id,
    zc_handle_t handle,
    size_t offset,
    const void* threshold,
    size_t size,
    bool* out_result
);
// ... (byptr, byref 版本)

// 数组长度比较 API
// <TODO> **与前一组比较 API 相同的扩展要求**
ZC_API zc_result_t zc_is_longer(
    zc_reader_id_t reader_id,
    zc_handle_t handle,
    size_t offset,
    size_t threshold_length,
    bool* out_result
);
ZC_API zc_result_t zc_is_shorter(
    zc_reader_id_t reader_id,
    zc_handle_t handle,
    size_t offset,
    size_t threshold_length,
    bool* out_result
);

// 数组内容比较 API
// <TODO> **与前一组比较 API 相同的扩展要求**
ZC_API zc_result_t zc_if_content_equal(
    zc_reader_id_t reader_id,
    zc_handle_t handle,
    size_t offset,
    const void* expected_array,
    size_t element_size,
    size_t element_count,
    bool* out_result
);
// ... (其他比较和极值查找 API)
```

### 2.4 算子 API

<TODO>

---

## 三、错误码枚举 `zc_result_t`

### 3.1 枚举定义

```c
typedef enum {
    // 通用成功/失败
    ZC_OK = 0,
    ZC_ERROR_GENERAL = -1,
    // 权限与状态错误
    ZC_ERROR_INVALID_HANDLE = -10,
    ZC_ERROR_PERMISSION_DENIED = -11,
    ZC_ERROR_BLOCK_NOT_READY = -12,
    ZC_ERROR_BLOCK_FROZEN = -13,    // 试图修改已 commit 块的 DTTA
    // 地址与边界错误
    ZC_ERROR_INVALID_ADDRESS = -20,
    ZC_ERROR_OUT_OF_BOUNDS = -21,
    // 类型系统错误
    ZC_ERROR_TYPE_MISMATCH = -30,
    ZC_ERROR_TYPE_NOT_FOUND = -31,   // 等价于 ZC_ERROR_VARIABLE_NOT_FOUND
    ZC_ERROR_TYPE_CONFLICT = -32,    // Store 时发生有类型区域重叠
    ZC_ERROR_TYPE_UNTYPED_OVERWRITE = -33, // Store 时无类型区域重叠 (Warning)
    // 变量与偏移量错误
    ZC_ERROR_VARIABLE_NOT_FOUND = -40,
    ZC_ERROR_OFFSET_NOT_FOUND = -41,
    ZC_ERROR_NOT_VARIABLE_START = -42, // Delete 时 offset 不是变量起始点 (非 UNTYPE)
    // 存储空间错误
    ZC_ERROR_DTTA_FULL = -50,        // DTTA 空间不足，描述符被缓存
    ZC_ERROR_INSUFFICIENT_SPACE = -51, // UserData 或 DTTA 整体空间不足
    // 指针与引用错误
    ZC_ERROR_INVALID_POINTER = -60,  // PTR/BYREF 指向块外或无效区域
    ZC_ERROR_INVALID_REFERENCE = -61, // BYREF 指向的类型不匹配
    // 参数与操作错误
    ZC_ERROR_INVALID_PARAMETER = -70,
    ZC_ERROR_INVALID_ACTION = -71,
    // 警告 (非致命错误)
    ZC_WARN_TYPE_UNTYPED_OVERWRITE = 1,
    ZC_WARN_RESERVE_FAILED = 2,
    ZC_WARN_DTTA_FRAGMENTED = 3
} zc_result_t;
```


附录
1. 递归解析结构体总大小（旧实现）
```c
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
```