# ZeroCore Internal 需求文档（v1.4.2-preview）

---

## 一、核心变更概览

- 类型编码标准化：采用 ECMA-335 CLI 规范的 `CorElementType` 枚举作为内部类型标识符并做私有解释，同时参考其附加信息逻辑指定了一套完备的附加 tokens 格式标准。
- 元数据存储位置：设计了一种灵活、低开销、紧邻数据的元数据存储方案，支持块内数据结构的动态变化。
- 设计了一批优先实现的 API。

---

## 二、详细设计与实现

### 2.1 类型编码：

- 详见 Zora 使用文档：1.1 类型描述符。
- 以下称为“指针”的，仅包括 0x0F PTR 和 0x10 BYREF，不包括 0x18 I 和 0x19 U。

### 2.2 元数据存储方案：动态类型标记区 (Dynamic Type Tag Area - DTTA)

- 位置选择：紧跟在 `UserData` 区域之后。
    ```
    | Header (64B) | UserData (N bytes) | [DTTA] | Padding |
    <----------- Block Total Size (512B Aligned) ----------->
    ```
  - 由于新的 API 设计，守卫线程不再需要，可以非常容易地在写入时进行边界检查，原空间释放出来，并且多预留一段空间（未来可以加入智能预留机制，根据申请的内存大小和线程的分配历史智能决定预留量）作为DTTA的预备空间。
  - 启用 Header 中的一个 64 位空间，记录 DTTA 条目的数量 `lut_entry_count`。

- DTTA 格式设计：
  - 两段设计，一张表和一条连续数字串。
  - 表是元数据查询表（Look-Up-Table），紧随 UserData。表内每一条目分成两项，将 UserData 中实际变量的起始偏移量与其在 DTTA 中元数据的起始偏移量一一对应，读取阶段条目顺序和偏移量顺序严格保证。

    ```c
    typedef struct zc_dtt_lut_entry {
        uint64_t data_offset;
        uint64_t desc_offset;
    } zc_dtt_lut_entry_t;
    ```

  - 表的每一行第二列的值正是连续数字串中一个类型描述符的起始偏移量。

- DTTA 的动态管理：
  - 初始化：`zc_writer_acquire_block` 成功后，`dtt_size` 初始化为 0。UserData 区域被视为 `ZC_TYPE_UNTYPED`。
  - 写入时标记：当用户调用 `zc_store_*` API 并指定了 `type` 参数时：
    - API 执行权限检查、地址翻译、边界检查。
      - 权限失败。
      - 内部故障导致地址错误。
      - 边界越界。
      - 不允许的范围重叠（仅当是两个 0x5C 无类型位流时允许重叠）。
    - API 将此次写入的 `(offset, type)` 作为一个新的 DTTA 条目插入或合并到现有的 DTTA 列表中：
      - 插入，如果新区域不与现有条目重叠则插入新条目。这会导致 DTTA 条目数增加。
      - 合并，如果新区域与现有某个条目重叠、或紧邻且在参数中指定行为、且是两个 0x5C 无类型位流则合并它们（更新 `length`）。
      - 合并，如果新区域与一个宽度动态的变量后序紧邻、或与其某个字段重叠且在参数中指定行为则将更改合并到这个动态宽度的变量中。
  - 空间检查：计算更新后的 DTTA 所需总大小。检查 `UserData` 结束位置 + 新 DTTA 大小是否仍在块的总大小范围内。
    - 如果空间足够，则更新 DTTA 内容。
    - 如果空间不足，则仅更新 DTTA 的元数据查询表，将类型描述符缓存在工作空间的 `其他缓存` 中；等待写入者调用删除操作时转移到空出的空间中，或是在其提交内存块之后检查是否有足够的缝隙，实在无法排下则内部争抢一块新内存块来专门记录。
  - 读取时校验：当用户调用 `zc_load` API 并指定了 `expected_type` 参数时：
    - 执行地址翻译、边界检查。
    - 读取并解析 DTTA。
    - 查找覆盖 `offset` 到 `offset+size` 范围的 DTTA 条目。
      - 如果找到且类型严格匹配 `expected_type` ，则加载数据。
      - 如果找到但类型不匹配，返回 `ZC_ERROR_TYPE_MISMATCH`。
      - 如果未找到任何条目，则返回 `ZC_ERROR_TYPE_UNWRITED`。
  - 提交后冻结：`zc_writer_commit_block` 被调用后，DTTA 内容应被视为不可变。后续的读取操作仍可进行类型校验，但不能再修改 DTTA。

### 2.3 写入者 API 行为

写入者通过 `zc_handle_t` 管理其拥有的块，直到 `commit` 或 `cancel`。

- Store API：
    ```c
    ZC_API zc_result_t zc_store(zc_handle_t handle, size_t offset, const void* value, const uint8_t* type_desc, size_t desc_len, zc_store_action action);
    ```
  - 行为：
    - 校验：
      - 权限失败。
      - 内部故障导致地址错误。
      - 边界越界。
      - 不允许的范围重叠（仅当是两个 0x5C 无类型位流时允许重叠）。
    - DTTA 更新：API 将此次写入的 `(offset, type)` 作为一个新的 DTTA 条目插入或合并到现有的 DTTA 列表中：
      - 插入，如果新区域不与现有条目重叠则插入新条目。
      - 合并，如果新区域与现有某个条目重叠、或紧邻且在参数中指定行为、且是两个 0x5C 无类型位流则合并它们（更新 `length`）。
      - 合并，如果新区域与一个宽度动态的变量后序紧邻、或与其某个字段重叠且在参数中指定行为则将更改合并到这个动态宽度的变量中。
      - 更新 `header -> lut_entry_count`。
    - 数据写入：`memcpy(base_addr + offset, value, width);`
    - 返回：`ZC_OK` 或相应的警告/错误码。

    ```c
    ZC_API zc_result_t zc_store_byptr(zc_handle_t handle, size_t ptr_offset, const void* value, const uint8_t* type_desc, size_t desc_len, zc_store_action action);
    ```
    - 行为：根据 `ptr_offset` 在 DTTA 中查找类型为 `0x0F (PTR)` 的条目，获取其指向的目标偏移量和长度。然后在目标偏移量处执行与 `zc_store` 相同的写入和 DTTA 更新逻辑。校验指针有效性（是否指向块内有效区域）。

    ```c
    ZC_API zc_result_t zc_store_byref(zc_handle_t handle, size_t ref_offset, const void* value, const uint8_t* type_desc, size_t desc_len, zc_store_action action);
    ```
    - 行为：根据 `ref_offset` 在 DTTA 中查找类型为 `0x10 (BYREF)` 的条目，获取其指向的目标偏移量、长度和**目标类型描述符**。校验传入的 `type_desc` 是否与目标类型描述符**精确匹配**。如果匹配，则在目标偏移量处执行写入和 DTTA 更新。如果不匹配，返回 `ZC_ERROR_TYPE_MISMATCH`。

- Copy API：
    ```c
    ZC_API zc_result_t zc_copy(zc_handle_t handle, size_t src_offset, size_t dst_offset, const uint8_t* type_desc, size_t desc_len, zc_copy_action action);
    ```
    - 行为：
      - 校验源和目标区域是否在块内。
      - 检查源和目标区域是否有类型冲突（除非是 `0x5C` 无类型位流）。
      - 执行 `memmove(base_addr + dst_offset, base_addr + src_offset, size);`
      - 根据 `action` 参数决定如何更新 DTTA：
        - `ZC_COPY_ACTION_PRESERVE_DST_TYPE`：目标区域类型不变，仅复制数据。
        - `ZC_COPY_ACTION_INHERIT_SRC_TYPE`：目标区域的 DTTA 条目被更新为源区域的类型（如果源有类型）。
        - `ZC_COPY_ACTION_CLEAR_TYPE`：目标区域变为 `0x5C` 无类型位流。
      - 返回 `ZC_OK` 或错误码。

    ```c
    ZC_API zc_result_t zc_copy_byptr(zc_handle_t handle, const uint8_t* src_ptr_desc, size_t src_desc_len, const uint8_t* dst_ptr_desc, size_t dst_desc_len, zc_copy_byptr_action action);
    ```

    ```c
    ZC_API zc_result_t zc_copy_byref(zc_handle_t handle, const uint8_t* src_ref_desc, size_t src_desc_len, const uint8_t* dst_ref_desc, size_t dst_desc_len, zc_copy_byref_action action);
    ```

- Delete API：
    ```c
    ZC_API zc_result_t zc_delete(zc_handle_t handle, size_t offset, const uint8_t* type_desc, size_t desc_len, zc_delete_action action);
    ```
    - 行为：
      - 校验（同 `zc_store`）。
      - 定位目标：查找 DTTA 中包含传入的 `offset` 的条目，验证是否是一个变量起始偏移量，除非这是一个 0x5C 无类型位流。
      - 删除逻辑：
        - 如果找到条目：
          - 一般情况将该条目标删除。
          - 如果是 0x5C 无类型位流，仅释放从 `offset` 开始到变量末尾的空间。更新该变量条目的 `length`（如果部分释放），或删除该条目（如果完全释放）。
          - 参数中可以指定要将删除条目的实际数据区清零。
          - 返回 `ZC_OK`。
        - 未找到：返回 `ZC_ERROR_VARIABLE_NOT_FOUND`。

    ```c
    ZC_API zc_result_t zc_delete_byptr(zc_handle_t handle, size_t ptr_offset, zc_delete_action action);
    ```
    - 行为：根据 `ptr_offset` 解析 `0x0F (PTR)`，然后对指针指向的目标区域执行 `zc_delete`。

    ```c
    ZC_API zc_result_t zc_delete_byref(zc_handle_t handle, size_t ref_offset, zc_delete_action action);
    ```
    - 行为：根据 `ref_offset` 解析 `0x10 (BYREF)`，然后对引用指向的目标区域执行 `zc_delete`。

### 2.3 读取者 API 行为

读取者通过 `zc_handle_t` 访问已请求的块。写入者也可以在提交或取消之前调用这些读取者 API。

- Load API：
    ```c
    ZC_API zc_result_t zc_load(zc_handle_t handle, size_t offset, void* out_value, const uint8_t* expected_desc, size_t expected_desc_len);
    ```
    - 行为：
      - 校验（同 `zc_store`）。
      - 类型匹配：
        - 根据 `offset` 和 `width`，查找覆盖该范围的 DTTA 条目。
        - 获取实际的类型描述符。
        - 精确匹配：对 `expected_desc` 和实际的类型描述符求异或，必须完全一致。
          - 附加 tokens 也必须匹配，除了 0x5C 无类型位流。
        - 如果匹配，执行 `memcpy(out_value, base_addr + offset, width);`，返回 `ZC_OK`。
        - 如果不匹配，返回 `ZC_ERROR_TYPE_MISMATCH`。
        - 如未找到任何类型描述符，返回 `ZC_ERROR_VARIABLE_NOT_FOUND`。
      - API 不校验 `out_value` 指针的有效性或大小，由调用者保证 `out_value` 指向的缓冲区足够。

    ```c
    ZC_API zc_result_t zc_load_byptr(zc_handle_t handle, size_t ptr_offset, size_t width, void* out_value, const uint8_t* expected_desc, size_t expected_desc_len);
    ```
    - 行为：根据 `ptr_offset` 解析 `0x0F (PTR)`，获取目标偏移量和长度。校验 `width` 是否小于等于目标长度。然后在目标偏移量处执行与 `zc_load` 相同的类型匹配和数据加载逻辑。

    ```c
    ZC_API zc_result_t zc_load_byref(zc_handle_t handle, size_t ref_offset, size_t width, void* out_value, const uint8_t* expected_desc, size_t expected_desc_len);
    ```
    - 行为：根据 `ref_offset` 解析 `0x10 (BYREF)`，获取目标偏移量、长度和**目标类型描述符**。校验传入的 `expected_desc` 是否与目标类型描述符**精确匹配**，且 `width` 是否等于目标长度。如果匹配，则加载数据。如果不匹配，返回 `ZC_ERROR_TYPE_MISMATCH`。

### 2.4 首批辅助 API

- Sizeof API：
    ```c
    ZC_API zc_result_t zc_sizeof(zc_handle_t handle, size_t offset, size_t* out_size);
    ```
  - 行为：根据 `offset`，查找对应的 DTTA 条目，返回其 `length` 字段；如果是指针则返回其指向的变量的 `length` 字段。如果未找到，返回 `ZC_ERROR_OFFSET_NOT_FOUND`。主要用于查询变量/区域的实际大小。

- Typeof API：
    ```c
    ZC_API zc_result_t zc_typeof(zc_handle_t handle, size_t offset, uint8_t* out_type_tag);
    ```
    - 行为：根据 `offset` 查找 DTTA 条目，返回其类型描述符的第一个字节（Tag）。如果未找到，返回 `ZC_ERROR_OFFSET_NOT_FOUND`。

    ```c
    ZC_API zc_result_t zc_typeof_byptr(zc_handle_t handle, size_t ptr_offset, uint8_t* out_type_tag);
    ```
    - 行为：根据 `ptr_offset` 解析 `0x0F (PTR)`，然后对指向的目标区域执行 `zc_typeof`。

- Compare API：
    ```c
    ZC_API zc_result_t zc_is_equal(zc_handle_t handle, size_t offset, const void* expected, size_t size, bool* out_result);
    ZC_API zc_result_t zc_is_unequal(zc_handle_t handle, size_t offset, const void* expected, size_t size, bool* out_result);
    ```
    - 行为：执行基础校验和边界检查 (`offset + size <= data_size`)。然后执行 `memcmp(base_addr + offset, expected, size)`。将结果写入 `out_result`。**不进行类型检查**。`is_unequal` 是 `is_equal` 的逻辑反。

    ```c
    // 仅对于数字类型有效
    ZC_API zc_result_t zc_is_bigger(zc_handle_t handle, size_t offset, const void* threshold, size_t size, bool* out_result);
    ZC_API zc_result_t zc_is_smaller(zc_handle_t handle, size_t offset, const void* threshold, size_t size, bool* out_result);
    ```
    - 行为：校验 `offset` 处的类型是否为数字类型 (`I1`, `U1`, `I2`, `U2`, `I4`, `U4`, `I8`, `U8`, `R4`, `R8`) 且 `size` 匹配其宽度。将 `base_addr + offset` 处的数据和 `threshold` 按相应类型进行数值比较。将结果写入 `out_result`。

    ```c
    // 仅对指向数字类型的指针有效
    ZC_API zc_result_t zc_is_bigger_byptr(zc_handle_t handle, size_t ptr_offset, const void* threshold, size_t size, bool* out_result);
    ZC_API zc_result_t zc_is_smaller_byptr(zc_handle_t handle, size_t ptr_offset, const void* threshold, size_t size, bool* out_result);
    ZC_API zc_result_t zc_is_bigger_byref(zc_handle_t handle, size_t ref_offset, const void* threshold, size_t size, bool* out_result);
    ZC_API zc_result_t zc_is_smaller_byref(zc_handle_t handle, size_t ref_offset, const void* threshold, size_t size, bool* out_result);
    ```
    - 行为：先解析指针或引用，然后对目标数据执行与 `zc_is_bigger`/`zc_is_smaller` 相同的数值比较。

    ```c
    // 仅对于数组类型或指向数组类型的指针有效
    ZC_API zc_result_t zc_is_longer(zc_handle_t handle, size_t offset, size_t threshold_length, bool* out_result);
    ZC_API zc_result_t zc_is_shorter(zc_handle_t handle, size_t offset, size_t threshold_length, bool* out_result);
    ```
    - 行为：校验 `offset` 处的类型是否为 `SZARRAY` 或 `ARRAY`。获取其元素数量（对于 `SZARRAY` 是 `4B` token，对于 `ARRAY` 需计算各维度乘积）。与 `threshold_length` 比较。将结果写入 `out_result`。

- Content API：
    ```c
    // 仅对数组或指向数组的指针有效
    ZC_API zc_result_t zc_if_content_equal(zc_handle_t handle, size_t offset, const void* expected_array, size_t element_size, size_t element_count, bool* out_result);
    ZC_API zc_result_t zc_ifnot_content_equal(zc_handle_t handle, size_t offset, const void* expected_array, size_t element_size, size_t element_count, bool* out_result);
    ```
    - 行为：校验 `offset` 处是否为数组类型且元素类型和大小匹配。执行逐元素 `memcmp`。`ifnot_content_equal` 是 `if_content_equal` 的逻辑反。

    ```c
    // 仅对以数字为基类的数组或指向以数字为基类的数组的指针有效
    ZC_API zc_result_t zc_if_content_bigger(zc_handle_t handle, size_t offset, const void* threshold_array, size_t element_size, size_t element_count, bool* out_result);
    ZC_API zc_result_t zc_if_content_smaller(zc_handle_t handle, size_t offset, const void* threshold_array, size_t element_size, size_t element_count, bool* out_result);
    ZC_API zc_result_t zc_ifnot_content_bigger(zc_handle_t handle, size_t offset, const void* threshold_array, size_t element_size, size_t element_count, bool* out_result);
    ZC_API zc_result_t zc_ifnot_content_smaller(zc_handle_t handle, size_t offset, const void* threshold_array, size_t element_size, size_t element_count, bool* out_result);
    ```
    - 行为：校验数组元素是否为数字类型。对每个元素执行数值比较（`>` 或 `<`）。`out_result` 为 `true` 当且仅当**所有**元素都满足条件。

    ```c
    ZC_API zc_result_t zc_content_biggest(zc_handle_t handle, size_t offset, void* out_max_element, size_t element_size, size_t element_count);
    ZC_API zc_result_t zc_content_smallest(zc_handle_t handle, size_t offset, void* out_min_element, size_t element_size, size_t element_count);
    ```
    - 行为：校验数组元素是否为数字类型。遍历数组，找到最大值/最小值元素，复制到 `out_max_element`/`out_min_element` 指向的缓冲区。

### 2.5 本批次 API 范围说明和未来 API 愿景
- 本批次的 API 仅包括直接的数据载入、载出、复制、删除这样的转移操作，和对块内数据进行有限次位运算可以完成的简单操作。
- 近期优先实现和优化的，是 0x5C 无类型位流的载入和载出，这是 ZeroCore 作为通用共享内存引擎的主责主业。
- 未来的 API 在计算方面的实现，以“覆盖调用 GPU 可以执行的操作”为愿景。底线是无块内可执行代码，可以成为外部代码的“运算草稿纸”，但是不能成为外部代码的代码空间。可能会表现得像存算一体，甚至可能成为未来存算一体芯片逐渐嵌入到传统计算设备过程中的微操作系统。
- 未来的 API 在逻辑方面的实现，以“覆盖 C# 的 System 中实现的数据结构”为愿景。
- 未来的 API 在性能优化方面的实现，总是以减少数据重复进出寄存器的次数为方向。
- 值得说明的是，这些 API 都是以 C 语言提供的，未来需要进行多语言包装，尤其是提供更易用的接口外形。

---

## 三、新增 zc_result_t 类型

为支持上述精细化的 API 行为，需扩充 `zc_result_t` 枚举类型：

```c
typedef enum {
    // 通用成功/失败
    ZC_OK = 0,
    ZC_ERROR_GENERAL = -1,

    // 权限与状态错误
    ZC_ERROR_INVALID_HANDLE = -10,
    ZC_ERROR_PERMISSION_DENIED = -11,
    ZC_ERROR_BLOCK_NOT_READY = -12, // 例如，块未被 acquire 或已 cancel
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
    ZC_WARN_TYPE_UNTYPED_OVERWRITE = 1, // 已定义，作为警告
    ZC_WARN_RESERVE_FAILED = 2,         // 预留空间失败
    ZC_WARN_DTTA_FRAGMENTED = 3         // DTTA 碎片化严重，性能可能下降
} zc_result_t;
```

此枚举类型为 API 调用提供了清晰、具体的反馈，便于调用者进行错误处理和调试。