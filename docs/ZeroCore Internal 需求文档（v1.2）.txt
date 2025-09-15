# ZeroCore Internal 需求文档（v1.2-preview）

## 一、概述

本文档描述 ZeroCore 引擎内部实现的需求与规范，用于指导 `zerocore_internal.c` 及相关内部组件的实现。

---

## 二、内存管理

### 2.1 内存池结构

```c
typedef struct {
    void* base_addr;          // 内存池起始地址
    atomic_size_t total_size; // 池总大小
    zc_stats_t stats;         // 统计信息
} zc_memory_pool_t;
```

### 2.2 块头部结构

```c
typedef struct zc_block_header {
    _Atomic uint32_t state;         // FREE, USING, CLEAN
    zc_writer_id_t writer;          // 数据写入者
    zc_time_t timestamp;            // 时间戳（纳秒）
    atomic_uint writer_refs;        // 写入者引用位图（32位）
    atomic_uint reader_refs;        // 读取者引用位图（32位）
    atomic_uint reader_visited;     // 读取者访问记录位图（32位）
    uint32_t data_size;             // 用户数据大小
} zc_block_header_t;
```

### 2.3 内存池初始化

1. 初始化内存池的元数据
2. 初始化内存池本身
3. 初始化第一个块头部，标记为 `FREE`，大小为整个池
4. 初始化统计信息

---

## 三、块状态机与原子操作

### 3.1 状态转换规则

| 转换 | 条件 | 操作者 |
|------|------|--------|
| FREE → USING | 写入者成功获取块 | 写入者 |
| USING → CLEAN | 清理者开始清理块 | 清理者 |
| CLEAN → FREE | 清理者完成合并 | 清理者 |
| FREE → CLEAN | 清理者合并相邻块 | 清理者 |

### 3.2 原子操作规范

所有状态转换必须使用 CAS 操作、在需要依赖状态标识确保正确的操作中间也需要添加 CAS 操作：

```c
// 示例：写入者尝试将状态从 FREE 改为 USING
uint32_t expected = ZC_STATE_FREE;
if (atomic_compare_exchange_strong(header->state, &expected, ZC_STATE_USING))
{
    // 成功获取块
}
else
{
    // 处理竞争或错误状态
}

// 示例：写入者在尝试获取块时将位图置位之后竞争性 CAS（这是防止清理者 CAS 成功一个正在被写入者尝试获取的块）
if (current_state == ZC_STATE_FREE && current_writer_refs == 0 && current_reader_refs == 0)
{
    uint32_t expected_refs = 0;
    if (atomic_compare_exchange_strong_explicit(
        header->writer_refs, 
        &expected_refs, 
        writer_id, 
        memory_order_acq_rel, 
        memory_order_acquire))
    {
        // 尝试将状态保持为 FREE（预防性 CAS）
        uint32_t expected_state = ZC_STATE_FREE;
        if (atomic_compare_exchange_strong_explicit(
            &header->state, 
            &expected_state, 
            ZC_STATE_FREE, 
            memory_order_acq_rel, 
            memory_order_acquire))
        {
            // 成功获取块，可以开始写入数据
        }
        else
        {
            // 状态已改变，清理者已经占用
        }
    }
    else
    {
        // 竞态，依据优先级决定处理方式
    }
}
else
{
    // 检查了一个非空闲块
}
```

### 3.3 内存序要求

- 状态修改：`memory_order_release`（写入端），`memory_order_acquire`（读取端）
- 位图操作：`memory_order_acq_rel`
- 统计计数：`memory_order_relaxed`

---

## 四、工作空间管理

### 4.1 工作空间结构

```c
typedef struct {
    // 心跳与缓存
    _Atomic uint64_t last_heartbeat;
    _Atomic uint64_t last_jump_dest;
    uint64_t cached_offset;
    
    // 消息系统
    zc_msg_space_t msg_space;
    
    // 线程注册信息
    zc_writer_registry_t writer_info;
    
    // 性能统计
    uint64_t writed_count;
    uint64_t message_count;
} zc_writer_workspace_t;

typedef struct {
    // 心跳与缓存
    _Atomic uint64_t last_heartbeat;
    uint64_t cached_offset;
    
    // 消息系统
    zc_msg_space_t msg_space;
    
    // 线程注册信息
    zc_reader_registry_t reader_info;
    
    // 性能统计
    uint64_t read_count;
    uint64_t message_count;
} zc_reader_workspace_t;
```

### 4.2 工作空间分配

- 每个线程注册时分配独立的工作空间
- 工作空间与 CPU 缓存行对齐
- 全局工作空间数组由内部线程管理，用于监控和统计

### 4.3 工作空间的一些原则

- 一个线程的工作空间中的所有内容只能由线程自己写入，对全局可读

---

## 五、消息系统（双缓冲实现）

### 5.1 消息缓冲区管理

```c
typedef struct {
    _Atomic uint32_t read_version;     // 自己作为接收方，已读取的最新版本
    _Atomic uint32_t write_version;    // 自己作为发送方，活动缓冲区的版本
    atomic_uintptr_t active_buffer;    // 当前活动缓冲区
    atomic_uintptr_t standby_buffer;   // 备用缓冲区
    atomic_size_t msg_count;           // 活动缓冲区中的消息数量
    atomic_bool buffer_swapping;       // 缓冲区切换中标志
    atomic_bool is_accumulating;       // 是否有积累的消息
} zc_msg_space_t;
```

### 5.2 消息发送流程

1. 将消息追加写入备用缓冲区
2. 访问接收方的工作空间，检查其读取版本是否等于当前活动缓冲区版本
3. 如果相等，将备用缓冲区切换为活动缓冲区，换下来的缓冲区可复用/释放
4. 如果不相等，将is_accumulating置为true
4. is_accumulating为true时，触发定期检查接收方读取版本的逻辑，一旦检测到接收方读取版本等于活动缓冲区版本，触发原子切换

### 5.3 消息接收流程

1. 比较本地读取版本缓存与发送方的活动缓冲区的版本
2. 如果不相等，读取新消息
3. 处理完成后原子更新本地读取版本缓存

---

## 六、清理者实现

### 6.1 清理者上下文

```c
typedef struct {
    zc_cleaner_id_t cleaner_id;       // 清理者ID
    zc_clean_strategy_t* strategy;    // 清理策略
    atomic_bool is_running;           // 运行标志
    size_t stride;                    // 并行步进
    size_t current_offset;            // 当前清理位置
    _Atomic uint64_t last_heartbeat;  // 上次心跳时间
    zc_stats_t local_stats;           // 本地统计
} zc_cleaner_context_t;
```

### 6.2 运行逻辑

1. **遍历**：从池首开始，根据每个 header 按块遍历内存池
2. **清理**：识别可清理块（USING 状态、所有读取者已访问、引用位图全零），将其状态更新为 CLEAN、清零读取者访问记录位图；然后继续遍历直到遇到不可清理的块，然后将中间经过的块的长度（包括header）累加更新到连续序列的第一个块 header 中
3. **合并**：识别空闲块（FREE 状态、位图全零）并缓存其偏移量；继续遍历，如果遇到的是一个新的空闲块，则将其标识 CAS 为 CLEAN 并累加其 size（包括 header），直到遇到非空闲块或 CAS 失败；然后再次检查缓存的块（连续序列的第一个块）是否仍然空闲，如果空闲则将其 data_size 原子地加上累计的后续 size，如果不空闲则只修改其后块（连续序列的第二个块）的 data_size 为累计的后续 size - HEADER_SIZE
4. **未读通知**：遍历到 USING 状态的块时检查其读取者访问记录位图是否与预期的“所有读取者均已访问”的状态一致，如果不一致则检查其时间戳与当前时间是否达到间隔阈值，如果达到则需检查未完成读取的读取者心跳并向其发送 ZC_MSG_MISSING_BLOCK 消息
5. **空闲块提示**：根据 zc_alloc_strategy 中定义的逻辑，对于例如长度足够且与前置内存块的写入者一致、或者长度过长可以分割等的空闲块，将其偏移量提供给所有或特定的writer，推荐它们在无法保持内存块使用连续性时跳转使用
6. **统计与背压处理**：遍历过程中累计相关信息，根据 zc_backpressure_strategy 中定义的逻辑计算出负载率，在负载率达到阈值时通知写入者和读取者、动态扩缩内存池等

### 6.3 多清理者并行优化

1. **清理者数量和步进偏移控制**
2. **多清理者无锁协同与工作空间共享**

### 6.4 说明

如前述，清理者其实可以视为系统应用，其运行和调度逻辑均应该允许通过配置接口定义和调整

---

## 七、注册表管理

### 7.1 注册表结构

```c
typedef struct {
    // 写入者注册表
    zc_writer_registry_t* writers;
    atomic_int writer_count;
    
    // 读取者注册表
    zc_reader_registry_t* readers;
    atomic_int reader_count;
    
    // 清理者注册表
    zc_cleaner_context_t* cleaners;
    atomic_int cleaner_count;
    
    // 保护锁（用于注册/注销操作）
    atomic_flag registry_lock;
} zc_registry_t;
```

### 7.2 ID 分配算法

ID按照掩码格式分配，使用uint32_t即至多有32个writer、每个writer至多有32个reader，按照注册的顺序分配，如有注销导致的空位允许新加入者使用

```c
// 生成读取者ID（高32位=writer_id，低32位=序列号）
zc_reader_id_t generate_reader_id(zc_writer_id_t writer_id) {
    static atomic_uint32_t reader_counter[ZC_MAX_WRITERS] = {0};
    uint32_t index = atomic_fetch_add(&reader_counter[writer_id], 1);
    return ((uint64_t)writer_id << 32) | (index & 0xFFFFFFFF);
}
```

---

## 八、错误处理与恢复

### 8.1 看门狗机制

- 定期检查各线程心跳
- 心跳超时线程的块会被强制回收
- 记录心跳丢失统计

### 8.2 状态不一致处理

- 使用校验和检测内存损坏
- 提供 `zc_internal_validate_block` 验证函数

### 8.3 优雅降级

- 内存不足时触发背压策略
- 提供降级统计和日志

---

## 九、性能优化

### 9.1 缓存优化

- 工作空间与缓存行对齐
- 热门数据与冷数据分离
- 预取下一个可能访问的块

### 9.2 NUMA 优化

- NUMA 节点感知的内存分配
- 线程绑定到特定 CPU 核心
- 跨节点访问统计与平衡

### 9.3 锁-free 优化

- 使用读-复制-更新（RCU）模式管理注册表
- 避免所有形式的锁，仅使用原子操作和CAS
- 实现无等待算法用于关键路径（尤其是写入者获取可用块）

---

## 十、跨平台适配

### 10.1 平台抽象层

构建专门的 zc_platform.h 和 zc_platform.c 提供跨平台抽象，所有需要的系统调用套用zc_platform提供的API

### 10.2 原子操作适配

- 使用 C11 stdatomic.h
- 确保所有原子操作有正确的内存屏障

---

## 十一、测试与验证

### 11.1 单元测试要求

- 覆盖所有状态转换路径
- 验证并发场景下的正确性
- 测试边界条件

### 11.2 集成测试场景

- 多写入者多读取者场景
- 背压触发与恢复场景
- 清理者并发操作场景

### 11.3 性能测试指标

- 单块分配/释放延迟 < 100ns
- 吞吐量 > 1M 操作/秒
- P999 延迟 < 1μs

---

## 十二、总结

本文档定义了 ZeroCore 内部实现的详细需求，包括内存管理、状态机、消息系统、清理者算法等关键组件的实现规范。所有实现必须遵循无锁编程原则，确保高性能和线程安全。

内部实现应优先考虑正确性和可靠性，其次优化性能。所有关键算法需有详细注释和测试用例覆盖。