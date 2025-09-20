# ZeroCore Internal 需求文档（v1.4.1-preview）

> **核心目标**：通过引入统一访问凭证 `zc_handle_t` 和配套的安全访问 API，实现对共享内存池的绝对安全管控与高效访问。将 ZeroCore 内部的地址翻译与权限校验逻辑封装为运行时符号表，对外提供类似“安全堆内存”的抽象。

---

## 一、核心变更概览

本次 v1.4.1 更新主要引入以下核心概念和机制：

1.  **统一访问凭证 (`zc_handle_t`)**：取代裸指针，作为访问内存块内数据的唯一合法“钥匙”。
2.  **安全访问 API 集**：所有对共享内存的读写操作必须通过 `zc_load_*`, `zc_store_*` 等函数进行，API 内部完成地址翻译、权限校验、类型检查（可选）和边界检查。
3.  **运行时符号表**：引擎内部维护一个映射表，将 `(zc_handle_t, offset)` 安全地翻译为物理内存地址，并附带元数据（如块状态、所属写入者、有效数据范围、可选的类型信息等）。
4.  **API 设计哲学**：提供强类型和弱类型（无类型/字节数组）两套访问接口，满足不同场景需求。强类型接口提供编译时和运行时的类型安全保证。

---

## 二、详细设计与实现

### 2.1 统一访问凭证 `zc_handle_t`

*   **定义**：
    ```c
    typedef struct zc_handle { int32_t token; uint32_t index; } zc_handle_t;
    // 正值 token 是有效 token，负值 token 是错误 token
    ```
    *   该句柄不是物理内存地址，而是一个由 ZeroCore 系统在 `zc_writer_acquire_block` 或 `zc_reader_poll_block` 成功时动态生成并返回给用户的“身份令牌”。
    *   生成算法应具备随机性和轻量级加密特性（例如，结合块在池中的索引、时间戳、随机盐进行哈希或异或），使其难以被伪造或预测。目标是防止外部代码构造非法句柄访问未授权内存。
    *   句柄与内部的 `zc_block_header_t` 及其对应的 `UserData` 区域存在快速映射关系。引擎内部维护一个（或多个，考虑 NUMA）映射表/查找结构（如哈希表、数组索引），能根据 `zc_handle_t` 快速定位到对应的块元数据和基地址。
*   **生命周期**：
    *   由 `zc_writer_acquire_block` / `zc_reader_poll_block` 生成并返回。
    *   在 `zc_writer_commit_block` / `zc_writer_cancel_block` / `zc_reader_release_block` 调用后，该句柄**立即失效**。引擎内部应将该句柄标记为无效或从映射表中移除/重置。
    *   尝试使用已失效的句柄进行任何 `zc_load`/`zc_store` 操作，将返回特定的错误码（如 `ZC_ERROR_INVALID_HANDLE`）。
*   **安全性**：
    *   **防伪造**：随机化/加密生成。
    *   **权限绑定**：句柄隐含了访问权限。写入者获取的句柄在 `commit`/`cancel` 前拥有写权限，之后只读（或失效）。读取者获取的句柄始终只读。API 内部会根据句柄来源和当前块状态校验操作合法性。
    *   **范围绑定**：句柄绑定到特定的 `UserData` 区域。API 内部会校验访问的 `offset` 是否在 `header->data_size` 范围内，防止越界。

### 2.2 安全访问 API 集

所有访问共享内存数据的操作，必须通过以下 API 进行。这些 API 是 ZeroCore 提供给外部用户的“安全门卫”。

#### 2.2.1 基础 Load/Store API (强类型)

提供针对常用基本数据类型的原子或非原子访问。**强类型 API 需要记录和校验类型信息**。

*   **类型信息记录方案**：
    *   **方案 A (推荐 - 运行时标记)**：在 `zc_block_header_t` 中增加一个可选的 `_Atomic uint32_t type_tag;` 字段（或利用 `reserved_flags` 的部分位）。当写入者首次通过**强类型 Store API** 写入数据时，API 内部会根据写入的类型（如 `uint32_t`）设置或更新这个 `type_tag`。后续的强类型 Load/Store API 会检查请求的类型与 `type_tag` 是否兼容。
    *   **方案 B (编译时辅助)**：提供一组宏，强制用户在获取句柄后“声明”预期类型。但这依赖用户自觉，安全性较低。
    *   **类型兼容性**：定义类型转换规则。例如，`int32_t` 与 `uint32_t` 可能被视为兼容（位模式相同），但 `float` 与 `int32_t` 不兼容。或者，允许所有整数类型间转换，但禁止与浮点数转换。具体规则需明确。
    *   **无类型数据**：如果块从未通过强类型 API 写入，`type_tag` 为默认值（如 0），表示“无类型”。此时，强类型 Load/Store API 可以执行，但需明确告知用户这是“强制转换”，可能存在风险（返回 `ZC_WARN_TYPE_MISMATCH` 或类似警告码，而非错误）。

*   **API 声明示例**：
    ```c
    // Load APIs
    ZC_API zc_result_t zc_load_u8(zc_handle_t handle, size_t offset, uint8_t* out_value);
    ZC_API zc_result_t zc_load_u16(zc_handle_t handle, size_t offset, uint16_t* out_value);
    ZC_API zc_result_t zc_load_u32(zc_handle_t handle, size_t offset, uint32_t* out_value);
    ZC_API zc_result_t zc_load_u64(zc_handle_t handle, size_t offset, uint64_t* out_value);
    ZC_API zc_result_t zc_load_i8(zc_handle_t handle, size_t offset, int8_t* out_value);
    ZC_API zc_result_t zc_load_i16(zc_handle_t handle, size_t offset, int16_t* out_value);
    ZC_API zc_result_t zc_load_i32(zc_handle_t handle, size_t offset, int32_t* out_value);
    ZC_API zc_result_t zc_load_i64(zc_handle_t handle, size_t offset, int64_t* out_value);
    ZC_API zc_result_t zc_load_f32(zc_handle_t handle, size_t offset, float* out_value);
    ZC_API zc_result_t zc_load_f64(zc_handle_t handle, size_t offset, double* out_value);

    // Store APIs (通常仅对未提交的写入者句柄有效)
    ZC_API zc_result_t zc_store_u8(zc_handle_t handle, size_t offset, uint8_t value);
    ZC_API zc_result_t zc_store_u16(zc_handle_t handle, size_t offset, uint16_t value);
    ZC_API zc_result_t zc_store_u32(zc_handle_t handle, size_t offset, uint32_t value);
    ZC_API zc_result_t zc_store_u64(zc_handle_t handle, size_t offset, uint64_t value);
    ZC_API zc_result_t zc_store_i8(zc_handle_t handle, size_t offset, int8_t value);
    ZC_API zc_result_t zc_store_i16(zc_handle_t handle, size_t offset, int16_t value);
    ZC_API zc_result_t zc_store_i32(zc_handle_t handle, size_t offset, int32_t value);
    ZC_API zc_result_t zc_store_i64(zc_handle_t handle, size_t offset, int64_t value);
    ZC_API zc_result_t zc_store_f32(zc_handle_t handle, size_t offset, float value);
    ZC_API zc_result_t zc_store_f64(zc_handle_t handle, size_t offset, double value);
    ```

*   **API 内部逻辑 (以 `zc_load_u32` 为例)**：
    1.  **句柄校验**：根据 `handle` 查找内部映射表，获取对应的 `zc_block_header_t* header` 和 `base_addr` (UserData 起始地址)。若句柄无效，返回 `ZC_ERROR_INVALID_HANDLE`。
    2.  **状态与权限校验**：
        *   检查 `header->state` 是否为 `USING` (或 `CLEAN`? 需定义读取者能否读 `CLEAN` 块，通常应禁止)。若非 `USING`，返回 `ZC_ERROR_BLOCK_NOT_READY` 或 `ZC_ERROR_BLOCK_INVALID`。
        *   对于读取者句柄，检查其 `reader_ref` 位是否仍为 `true` (确保未提前释放)。
        *   对于写入者句柄，若块已 `commit`，则禁止 `store`，`load` 可能允许。
    3.  **边界校验**：检查 `offset + sizeof(uint32_t) <= header->data_size`。若越界，返回 `ZC_ERROR_OUT_OF_BOUNDS`。
    4.  **类型校验 (如果启用)**：
        *   获取 `header->type_tag`。
        *   检查请求类型 (`uint32_t`) 与 `type_tag` 是否兼容。若不兼容且非“无类型”，返回 `ZC_ERROR_TYPE_MISMATCH`。若是“无类型”，可执行加载并可能返回 `ZC_WARN_TYPE_UNTYPED`。
    5.  **地址翻译**：计算物理地址 `target_addr = base_addr + offset`。
    6.  **数据加载**：执行 `*out_value = *(volatile uint32_t*)target_addr;` (使用 `volatile` 确保每次访问都从内存读取，避免编译器优化)。
    7.  **返回**：返回 `ZC_OK` 或相应的警告/错误码。

#### 2.2.2 无类型/批量 Load/Store API

用于处理结构体、数组或类型未知的数据。

*   **API 声明示例**：
    ```c
    // 无类型 Load/Store (按字节)
    ZC_API zc_result_t zc_load_bulk(zc_handle_t handle, size_t offset, void* buffer, size_t size);
    ZC_API zc_result_t zc_store_bulk(zc_handle_t handle, size_t offset, const void* buffer, size_t size);

    // 批量 Load/Store (数组)
    // 例如：加载 count 个 uint32_t 到 out_array
    ZC_API zc_result_t zc_load_array_u32(zc_handle_t handle, size_t offset, uint32_t* out_array, size_t count);
    ZC_API zc_result_t zc_store_array_u32(zc_handle_t handle, size_t offset, const uint32_t* array, size_t count);
    // ... 其他类型类似
    ```

*   **API 内部逻辑 (以 `zc_load_bulk` 为例)**：
    1.  **句柄、状态、权限校验**：同强类型 API。
    2.  **边界校验**：检查 `offset + size <= header->data_size`。若越界，返回 `ZC_ERROR_OUT_OF_BOUNDS`。
    3.  **类型校验 (宽松)**：通常不进行严格类型检查，或仅检查是否为“无类型”或兼容的“字节流”。可以记录此次访问为“无类型访问”，后续强类型访问可能收到警告。
    4.  **地址翻译**：计算物理地址 `target_addr = base_addr + offset`。
    5.  **数据搬运**：执行 `memcpy(buffer, (void*)target_addr, size);`。
    6.  **返回**：返回 `ZC_OK`。

#### 2.2.3 比较与查询 API

*   **API 声明示例**：
    ```c
    // 比较内存区域是否相等 (常用于校验或查找)
    ZC_API zc_result_t zc_is_equal(zc_handle_t handle, size_t offset, const void* expected, size_t size, bool* out_result);
    ZC_API zc_result_t zc_is_unequal(zc_handle_t handle, size_t offset, const void* expected, size_t size, bool* out_result);

    // 查询块信息 (替代或补充原有的 zc_block_size, zc_block_timestamp)
    ZC_API zc_result_t zc_handle_size(zc_handle_t handle, size_t* out_size); // 用户数据大小
    ZC_API zc_result_t zc_handle_timestamp(zc_handle_t handle, zc_time_t* out_timestamp);
    ZC_API zc_result_t zc_handle_writer_id(zc_handle_t handle, zc_writer_id_t* out_writer_id);
    ZC_API zc_handle_state(zc_handle_t handle, uint16_t* out_state); // 返回块状态
    ZC_API zc_handle_valid(zc_handle_t handle, bool* out_is_valid); // 检查句柄当前是否有效 (未释放)
    ```

*   **内部逻辑**：主要进行句柄有效性、状态、边界检查，然后执行相应的内存比较或读取元数据操作。

### 2.3 新增 `zc_result_t` 枚举值

为支持新的安全机制和类型系统，需扩展错误码：

```c
typedef enum {
    // ... 原有错误码 ...
    ZC_ERROR_INVALID_HANDLE,      // 提供的 zc_handle_t 无效或已失效
    ZC_ERROR_BLOCK_NOT_READY,     // 块状态不允许当前操作 (如读取 FREE/CLEAN 块)
    ZC_ERROR_BLOCK_INVALID,       // 块状态为 CORRUPT
    ZC_ERROR_OUT_OF_BOUNDS,       // 访问偏移量超出 UserData 范围
    ZC_ERROR_TYPE_MISMATCH,       // 强类型访问时，请求类型与块记录类型不兼容
    ZC_ERROR_PERMISSION_DENIED,   // 权限不足 (如用读取者句柄尝试写入)
    ZC_WARN_TYPE_UNTYPED,         // 强类型访问无类型数据块 (非致命，但需注意)
    ZC_WARN_TYPE_MISMATCH,        // 类型不兼容但允许转换 (可选，取决于策略)
    // ... 可能还有其他，如 ZC_ERROR_HANDLE_REVOKED (句柄被强制回收) ...
} zc_result_t;
```

### 2.4 对现有接口的影响

*   **`zc_writer_acquire_block` / `zc_reader_poll_block`**：
    *   不再直接返回 `UserData` 的裸指针 (`void*`)。
    *   返回值或通过 `out_handle` 参数返回一个 `zc_handle_t`。
    *   原有的 `zc_block_handle_t*` 类型**可能被废弃或重定义为 `zc_handle_t` 的别名**。需要评估向后兼容性。**建议在 v1.4.1 中将 `zc_block_handle_t` 重定义为 `zc_handle_t`，并标记旧的指针返回方式为废弃。**
*   **`zc_writer_commit_block` / `zc_writer_cancel_block` / `zc_reader_release_block`**：
    *   参数从 `zc_block_handle_t*` 改为 `zc_handle_t`。
    *   调用后，传入的 `zc_handle_t` 立即失效。
*   **`zc_block_size` / `zc_block_timestamp`**：
    *   这些函数可以保留，但其参数 `zc_block_handle_t*` 应指向一个 `zc_handle_t` (如果重定义了类型)。
    *   **更推荐的做法是废弃它们，引导用户使用新的 `zc_handle_size`, `zc_handle_timestamp` 等查询 API，这些 API 返回 `zc_result_t` 便于错误处理。**

### 2.5 性能考量

*   **开销**：每次访问都增加了句柄查找、权限校验、边界检查的开销。
*   **优化**：
    *   **句柄映射**：使用高效的查找结构（如直接数组索引，如果句柄设计为包含索引；或哈希表）。
    *   **内联与编译优化**：关键的校验逻辑（如边界检查 `offset + size <= header->data_size`）应尽可能内联，并利用编译器优化。
    *   **缓存局部性**：`zc_block_header_t` 和映射表应设计得利于缓存。
    *   **批量操作**：鼓励使用 `zc_load_bulk`/`zc_store_bulk` 或数组 API 减少函数调用次数。
*   **权衡**：安全性和正确性是首要目标。性能优化应在保证安全的前提下进行。对于极致性能场景，未来可考虑提供“不安全但快速”的访问模式（需显式启用并承担风险）。

### 2.6 与守卫线程的协作

*   **守卫线程 (`zc_guard_on_detect`)** 的核心任务是检测 `Trailer` 的 `canary` 是否被篡改，这发生在**物理内存层面**。
*   新的 `zc_handle_t` 和 API 机制是在**逻辑访问层面**增加安全校验。
*   **两者关系**：它们是**互补**的。
    *   `zc_handle_t` + API 防止**合法用户**的**意外或恶意越界访问**（通过校验 offset）。
    *   守卫线程防止**任何方式**（包括绕过 API 的非法访问、硬件错误、极端并发 bug）导致的**物理内存越界写入**破坏 `Trailer`。
*   **协同**：如果守卫线程检测到 `CORRUPT`，它会通过消息系统报告。此时，任何尝试通过 `zc_handle_t` 访问该块的 API 调用，在状态校验步骤就会失败（返回 `ZC_ERROR_BLOCK_INVALID`），从而阻止用户接触到已损坏的数据。

---

## 三、总结 (v1.4.1-preview)

本次更新通过引入 `zc_handle_t` 和一套完整的安全访问 API，将 ZeroCore 共享内存池的访问模型从“直接操作裸指针”转变为“通过受控凭证和函数调用访问”。这带来了以下核心优势：

1.  **绝对安全管控**：有效防止越界访问、使用已释放句柄、权限违规等内存安全问题。
2.  **高效访问**：在保证安全的前提下，通过优化的地址翻译和校验逻辑，力求最小化性能开销。
3.  **类型安全 (可选)**：强类型 API 提供了额外的类型检查层，减少类型错误。
4.  **运行时符号表**：构建了引擎内部的元数据映射，为未来的监控、调试、动态优化（如基于访问模式的预取）打下基础。
5.  **抽象提升**：对外部用户而言，ZeroCore 内存池更像一个“安全的、带元数据的堆”，降低了使用心智负担和出错概率。

此版本是迈向更安全、更健壮 ZeroCore 的重要一步，为后续的功能（如更复杂的类型系统、访问模式分析、自动内存管理扩展）奠定了坚实的基础。