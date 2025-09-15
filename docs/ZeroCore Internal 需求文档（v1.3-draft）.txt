# ZeroCore Internal 需求文档（v1.3-draft）

## 一、概述

本文档描述 ZeroCore 引擎内部实现的需求与规范，用于指导 `zerocore_internal.c` 及相关内部组件的实现。v1.3 版本在 v1.2 基础上进行了系统性重构与增强，重点改进方向包括：

- **清理者架构的精细化控制与多线程协同机制**
- **内存块头部原子操作的并行化优化**
- **明确“写者优先”为设计原则，并据此调整状态机与调度策略**
- **引入轻量级数据完整性校验机制以支持生产环境安全**
- **MessageRing 机制全面落地**：双向链表结构、当值者选举、消息路由策略、嵌套支持
- **安全机制分层设计**：阻断型信号量守卫 + 可选 CRC + 固定魔数兜底
- **内存块头部极致瘦身**：优化为 40 字节，释放用户空间
- **块布局重新规划**：Header + UserData + Trailer + Padding → 整体对齐 512 字节

所有实现必须继续遵循 **无锁编程、高性能、跨平台、低延迟、写者优先、生产安全** 的核心目标。

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

#### 设计目标：
- 支持独立原子位操作
- 保留状态机、时间戳、引用追踪能力

#### 结构定义：

```c
#define ZC_MAX_WRITERS       32
#define ZC_MAX_READERS_PER   32

typedef struct zc_block_header {
    _Atomic uint16_t   state;       // FREE=0, USING=1, CLEAN=2
    uint16_t  reserved_flags;       // 未来扩展位
    uint32_t           writer_id;       // 写入者ID
    uint64_t          data_size;       // 用户数据大小（单位：字节）
    zc_time_t        timestamp;       // 写入时间戳

    // === 并行引用位图（独立原子bool）===
    _Atomic bool     writer_ref[ZC_MAX_WRITERS];     // 32B
    _Atomic bool     reader_ref[ZC_MAX_READERS_PER]; // 32B
    _Atomic bool     reader_visited[ZC_MAX_READERS_PER]; // 32B
} zc_block_header_t;
```

#### 性能优势分析：

| 特性 | 说明 |
|------|------|
| **真正并行写入** | 多个读取者可同时设置各自的 `reader_ref[i]` 而不产生冲突，无需CAS-loop |
| **减少原子操作开销** | `_Atomic bool` 在x86-64上通常编译为单条 `lock bts` 或 `cmpxchg`，远快于完整整数RMW |
| **编译器优化友好** | 可通过 `__attribute__((packed))` 和静态索引计算生成高效汇编 |
| **调试信息丰富** | 直接观察哪个线程未释放引用，便于故障排查 |

### 2.3 内存块布局

```
[ Header (64B) ] + [ UserData (N bytes) ] + [ Trailer (24B) ] + [ Padding ]
```

#### 块尾守卫结构（v1.3.3）：

```c
typedef volatile struct {
    uint64_t guard_signal;   // 守卫量：初始值为魔数，被篡改即触发报警
    uint64_t done;     // 停车标志：非0=运行中，0=停止监测
    uint64_t checksum; // 可选CRC（默认0，启用时计算）
} zc_block_tail_t;
```

### 2.4 内存池初始化

1. 初始化内存池的元数据
2. 初始化内存池本身
3. 初始化第一个块头部，标记为 `FREE`，大小为整个池
4. 初始化统计信息

---

## 三、块状态机与原子操作

### 3.1 状态转换规则

| 转换 | 条件 | 操作者 | 优先级 |
|------|------|--------|-------|
| FREE → USING | 写入者成功获取且无竞态 | 写入者 | ⭐⭐⭐⭐⭐（最高） |
| FREE → CLEAN | 开始合并相邻空闲块 | 清理者 | ⭐⭐☆ |
| USING → CLEAN | 所有 reader 已 visited & ref=0 & writer ref=0 | 清理者 | ⭐⭐⭐☆ |
| USING → CLEAN | （异常情况）心跳超时强制回收 | 看门狗/清理者 | ⭐ |
| USING → CORRUPT | 检测到内存块被篡改 | 守卫线程 | ⭐⭐⭐⭐ |
| CLEAN → FREE | 完成清理或合并 | 清理者 | ⭐⭐☆ |
| CORRUPT → CLEAN | 开始清理 | 清理者 | ⭐⭐☆ |

### 3.2 原子操作规范

#### 写入者获取块流程

```c
zc_result_t zc_try_acquire_for_write(zc_block_header_t* header, 
                                     zc_writer_id_t wid) {
    // 快速路径：检查是否完全空闲
    if (likely(atomic_load(&header->state) == ZC_STATE_FREE &&
               !any_writer_ref_set(header) && 
               !any_reader_ref_set(header))) {

        // 第一步：预占位自己（局部原子）
        atomic_store_explicit(&header->writer_ref[wid], true, memory_order_relaxed);

        // 第二步：防御性CAS确认整体状态未变
        uint8_t expected = ZC_STATE_FREE;
        if (atomic_compare_exchange_strong_explicit(
                &header->state, &expected, ZC_STATE_FREE,
                memory_order_acq_rel, memory_order_acquire)) {
            // 成功！进入写入模式
            return ZC_OK;
        } else {
            // 状态已被改变（如被清理者占用），清除自己的引用
            atomic_store_explicit(&header->writer_ref[wid], false, memory_order_relaxed);
            return ZC_ERROR_BUSY;
        }
    }

    // 慢路径：存在竞态，依据优先级决策
    uint8_t current_state = atomic_load(&header->state);
    if (current_state == ZC_STATE_FREE) {
        // 存在其他 writer_ref 设置 → 判断优先级
        int my_priority = wid;
        int lowest_set = find_lowest_set_writer_ref(header);
        if (my_priority = lowest_set) {
            // 我优先级最高，抢占
            return attempt_preemptive_acquire(header, wid);
        } else {
            return ZC_ERROR_BUSY; // 放弃，寻找下一个块
        }
    }

    return ZC_ERROR_BUSY;
}
```

> **写者优先体现**：
> - 小 ID 写入者具有更高优先级
> - 不进行 busy-wait，立即放弃低优先级竞争，转向下一候选块
> - 减少竞态僵持时间，提升整体吞吐

### 3.3 内存序要求

- 状态修改：`memory_order_release`（写入端），`memory_order_acquire`（读取端）
- 位图操作：`memory_order_acq_rel`
- 统计计数：`memory_order_relaxed`

---

## 四、工作空间管理

### 4.1 工作空间结构

```c
// 写入者工作空间
typedef struct {
    // 心跳与缓存
    _Atomic uint64_t last_heartbeat;
    _Atomic uint64_t last_jump_dest;
    uint64_t cached_offset;

    // 线程注册信息
    zc_writer_registry_t writer_info;

    // 消息系统
    zc_msg_space_t msg_space;

    // 性能统计
    uint64_t writed_count;
    uint64_t message_count;
} zc_writer_workspace_t;

//读取者工作空间
typedef struct {
    // 心跳与缓存
    _Atomic uint64_t last_heartbeat;
    _Atomic uint64_t last_jump_dest;
    uint64_t cached_offset;

    // 线程注册信息
    zc_reader_registry_t reader_info;

    // 消息系统
    zc_msg_space_t msg_space;

    // 性能统计
    uint64_t read_count;
    uint64_t message_count;
} zc_reader_workspace_t;

// 清理者共享工作空间
typedef struct {
    // 消息系统
    zc_msg_space_t msg_space_for_writers[ZC_MAX_WRITERS];
    zc_msg_space_t msg_space_for_readers[ZC_MAX_WRITERS][ZC_MAX_READERS_PER];

    // 指导地址
    zc_free_block_guidance blk_guide[ZC_MAX_WRITERS];

    // 性能统计
    _Atomic uint64_t clean_count;
    _Atomic uint64_t message_count;
} zc_cleaner_workspace_t;
```

### 4.2 工作空间分配

- 每个线程注册时分配独立的工作空间，只能由线程自己写入，对全局可读
- 所有清理者共享同一个清理者工作空间

---

## 五、消息系统

### 5.1 消息缓冲区管理

```c
typedef struct {
    _Atomic uint16_t read_version;     // 自己作为接收方，已读取的最新版本
    _Atomic uint16_t write_version;    // 自己作为发送方，活动缓冲区的版本
    atomic_bool buffer_swapping;       // 缓冲区切换中标志
    atomic_bool is_accumulating;       // 是否有积累的消息
    atomic_uintptr_t active_buffer;    // 当前活动缓冲区
    atomic_size_t msg_count;           // 活动缓冲区中的消息数量
    atomic_uintptr_t standby_buffer;   // 备用缓冲区
} zc_msg_space_t;
// 32B
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
    zc_cleaner_id_t cleaner_id;
    atomic_int cleaner_state;        // 当前清理者状态
    atomic_size_t current_offset;    // 当前清理者处理的块头偏移
    _Atomic uint64_t last_heartbeat; // 心跳
    atomic_int desired_count;        // 期望清理者数量
    uint64_t thread_id;              // 系统线程ID

    zc_memory_pool_t* pool;          // 池指针
    zc_cleaner_shared_workspace_t* shared_ws; // 共享工作空间指针

    // MessageRing 集成
    zc_messagering_node_t* ring_node;
} zc_cleaner_context_t;
```

### 6.2 内部消息 MessageRing 集成

#### 内部消息发送模式：

| 模式 | 描述 | 适用场景 |
|------|------|----------|
| **单向单包** | 选择距离更近方向发送，包头含 dst，沿途节点检查 dst，命中则处理+继续传递，回源/超时/处理完后释放 | 常规通知、负载提示 |
| **双向双包** | 向左右同时发送相同包，逻辑同上，确保至少一个方向送达 | 关键指令、合并确认、主从选举 |

> 消息传递均为指针传递，零拷贝。

#### 内部消息体系：

(待具体设计)

> 所有清理者通过 `MessageRing` 实现：
> - 负载均衡沟通
> - 清理操作相邻清理者协作
> - 系统策略投票（如动态增减）

#### 当值者选举逻辑：

1. **初始化**：环线创建时，第一个发送的消息包携带 `ownership_token = 1`，接收者成为当值者。
2. **传递**：当值者发送新消息时，将 `ownership_token` 注入包中，接收者成为新当值者。
3. **保持**：若当值者无消息发送，保留身份。
4. **超时重选**：守护者线程检测超时，随机指定新当值者并广播 `MSG_OWNER_TRANSFER`。

#### 当值者职责：

- 管理 **清理者共享工作空间**，其他清理者可读
- 代表环线与 **外部系统通信**（如监控线程、外部代理）
- 发起 **全池调整投票**（如块大小重配、策略切换）

### 6.3 运行逻辑

#### 主循环框架：

```c
void zc_cleaner_main_loop(zc_cleaner_context_t* ctx) {
    while (ctx->is_running) {
        // 1. 扫描并清理块
        scan_and_clean(ctx);

        // 2. 处理 MessageRing 消息
        process_ring_messages(ctx);

        // 3. 若为当值者，更新共享状态
        if (ctx->is_leader) {
            update_shared_workspace(ctx);
            broadcast_shared_state_if_changed(ctx);
        }

        // 4. 心跳与当值者续约
        send_heartbeat_and_renew_ownership(ctx);
    }
}
```

#### 详细流程：

1. **遍历**：从池首开始，根据每个 header 按块遍历内存池
2. **清理**：识别可清理块（USING 状态、所有读取者已访问、引用位图全零），将其状态更新为 CLEAN、清零读取者访问记录位图；然后继续遍历直到遇到不可清理的块，然后将中间经过的块的长度（包括header）累加更新到连续序列的第一个块 header 中
3. **合并**：识别空闲块（FREE 状态、位图全零）并缓存其偏移量；继续遍历，如果遇到的是一个新的空闲块，则将其标识 CAS 为 CLEAN 并累加其 size（包括 header），直到遇到非空闲块或 CAS 失败；然后再次检查缓存的块（连续序列的第一个块）是否仍然空闲，如果空闲则将其 data_size 原子地加上累计的后续 size，如果不空闲则只修改其后块（连续序列的第二个块）的 data_size 为累计的后续 size - HEADER_SIZE
4. **未读通知**：遍历到 USING 状态的块时检查其读取者访问记录位图是否与预期的"所有读取者均已访问"的状态一致，如果不一致则检查其时间戳与当前时间是否达到间隔阈值，如果达到则需检查未完成读取的读取者心跳并向其发送 ZC_MSG_MISSING_BLOCK 消息
5. **空闲块提示**：根据 zc_alloc_strategy 中定义的逻辑，对于例如长度足够且与前置内存块的写入者一致、或者长度过长可以分割等的空闲块，将其偏移量提供给所有或特定的writer，推荐它们在无法保持内存块使用连续性时跳转使用
6. **统计与背压处理**：遍历过程中累计相关信息，根据 zc_backpressure_strategy 中定义的逻辑计算出负载率，在负载率达到阈值时通知写入者和读取者、动态扩缩内存池等

### 6.4 多清理者并行优化

#### 6.4.1 Strided Scanning（步进扫描）

- 初始偏移：`offset = cleaner_id * stride`
- 步长：`stride = max(512, pool_size / (cleaner_count * 4))`
- 动态划分每两个相邻清理者之间的步进偏移，避免重叠、降低全局最大清理延迟

#### 6.4.2 动态增减清理者

```c
typedef struct zc_scaling_policy {
    bool (*should_scale_up)(zc_stats_t*, int current);
    bool (*should_scale_down)(zc_stats_t*, int current);
    int max_cleaners;
    void* ctx;
} zc_scaling_policy_t;

// 示例策略：基于负载率
bool default_scale_up(zc_stats_t* s, int cur) {
    return s->used_bytes * 100 / s->total_bytes > 85 && cur < policy->max_cleaners;
}

bool default_scale_down(zc_stats_t* s, int cur) {
    return s->used_bytes * 100 / s->total_bytes < 40 && cur > 1;
}
```

#### 6.4.3 当值者决策

- 当值清理者负责：
  - 汇总统计
  - 决定是否扩容/缩容
  - 提供新注册线程的内部资源初始化
- 从清理者定期上报状态

```c
void zc_cleaner_heartbeat_to_leader(zc_cleaner_context_t* ctx) {
    if (!ctx->is_leader) {
        zc_message_t hb = {.type=ZC_MSG_HEARTBEAT, .src=ctx->cleaner_id};
        zc_cleaner_send_to_leader(&hb);
    }
}
```

### 6.5 说明

清理者视为系统应用，其运行和调度逻辑均应该允许通过配置接口定义和调整

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

### 7.3 清理者生命周期管理

新增 API：

```c
// 内部函数
zc_result_t zc_internal_spawn_cleaner_thread(void);
zc_result_t zc_internal_terminate_cleaner_thread(zc_cleaner_id_t id);

// 配置接口
ZC_API zc_result_t zc_set_scaling_policy(const zc_scaling_policy_t* policy);
```

> 支持运行时热插拔清理者线程

---

## 八、块尾守卫
### 8.1 安全层级：

| 层级 | 机制 | 触发条件 | 适用场景 |
|------|------|----------|----------|
| L1 | `guard_signal` | 越界写入检测 | 默认开启 |
| L2 | `checksum` 校验 | 外部线程显式触发 / 调试模式 | 可选开启 |

### 8.2 守卫线程机制：

- 每个写入者注册时配套创建对应的守卫线程，实现**纯软件、低延迟越界写监测**。
- 守卫线程包括其任务空间、阻塞在信号量上的主循环、检测函数、阻断函数、报告函数

#### 守卫线程任务空间：

```c
typedef struct {
    zc_block_tail_t* block;       // 监测目标
    atomic_bool      ready;       // 是否已绑定块
    pthread_cond_t   go;          // 启动条件变量
    pthread_mutex_t  mutex;       // 保护条件变量
    zc_writer_id_t   writer_id;   // 所属写入者ID
    void**           user_handle; // 外部线程持有的 handle 指针缓存（关键！）
    atomic_bool      triggered;   // 是否已触发报警
} zc_guard_task_t;
```

#### 阻断机制实现：

```c
// 在 zc_writer_acquire_block 内部：
ZC_API void* zc_writer_acquire_block(zc_writer_id_t wid, size_t size) {
    zc_block_header_t* header = find_and_acquire_block(wid, size);
    if (!header) return NULL;

    void* user_ptr = get_user_data(header);

    // 缓存用户 handle 指针（关键步骤）
    zc_guard_task_t* guard = get_guard_task_for_writer(wid);
    guard->user_handle = &user_ptr; // 注意：是 handle 的地址！

    // 初始化守卫段
    zc_block_tail_t* tail = get_guard_tail(header);
    tail->canary = generate_random_canary();
    tail->done = 1;

    // 唤醒守卫线程
    zc_guard_on_attach(guard, tail);

    return user_ptr; // 返回给用户
}

// 阻断函数
static void zc_guard_stop_writer(zc_guard_task_t* task) {
    if (!task->user_handle) return;

    // 方案1：置 NULL → 用户写入触发 SIGSEGV
    // 方案2：指向告警页 → 用户写入触发自定义信号
    void* poison_ptr = (void*)0x1; // 或 mmap 一个 PROT_NONE 区域

    // 篡改用户持有的 handle 值
    *task->user_handle = poison_ptr;
}
```

#### 报告函数逻辑：

- 守卫触发报告时，将内存块的状态修改为CORRUPT，然后访问其依附的写入者的工作空间，向清理者发送消息报告完整的异常信息，同时触发 Hook
- 清理者一方面在遍历时遇到状态为CORRUPT的块直接执行清理，另一方面接收守卫发送的消息为对应的写入者打上"不可信"标记并向外部监控应用报告

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
- **新增测试场景**：
| 编号 | 场景 | 目标 |
|------|------|------|
| TC-RING-1 | 单向/双向消息传递正确性 | 验证路由逻辑 |
| TC-RING-2 | 当值者超时移交 | 验证高可用 |
| TC-RING-3 | 嵌套环线通信 | 验证扩展性 |
| TC-CRC-1 | 按需触发CRC校验 | 验证可选安全层 |
| TC-GUARD-3 | 故意越界写入触发守卫 | 验证阻断与报告 |
| TC-GUARD-4 | 多写入者并发触发守卫 | 验证线程安全 |
| TC-GUARD-5 | 守卫线程唤醒/休眠压力 | 验证生命周期管理 |
| TC-GUARD-6 | 高级语言 wrapper 指针校验 | 验证作弊防御 |

### 11.3 性能测试指标

- 单块分配/释放延迟 < 100ns
- 吞吐量 > 1M 操作/秒
- P999 延迟 < 1μs

---

## 十二、总结

本文档定义了 ZeroCore 内部实现的详细需求，包括内存管理、状态机、消息系统、清理者算法等关键组件的实现规范。所有实现必须遵循无锁编程原则，确保高性能和线程安全。

### 主要变更（v1.3）

| 模块 | 变更内容 |
|------|---------|
| **清理者** | 支持动态伸缩、引入 MessageRing 实现动态协同 |
| **原子操作** | 位图拆分为独立 `_Atomic bool` 字段，实现真并行 |
| **设计原则** | 明确"写者优先"，优化状态机抢占逻辑 |
| **安全性** | 引入 trailer guard 与 CRC，支持生产级防篡改 |
| **安全增强** | 引入守卫线程+阻断机制，实现纯软件越界监测 |

内部实现应优先考虑正确性和可靠性，其次优化性能。所有关键算法需有详细注释和测试用例覆盖。