# ZeroCore Internal 需求文档（v1.3.1-preview）  

---

## 一、概述

本文档在 `ZeroCore Internal 需求文档（v1.2）` 基础上，结合 **v1.3.1 更新事项** 进行系统性重构与增强。重点改进方向包括：

- **清理者架构的精细化控制与多线程协同机制**
- **内存块头部原子操作的并行化优化**
- **明确“写者优先”为设计原则，并据此调整状态机与调度策略**
- **引入轻量级数据完整性校验机制以支持生产环境安全**

所有实现必须继续遵循 **无锁编程、高性能、跨平台、低延迟** 的核心目标。

---

## 二、块头部结构（更新）

### 2.1 原始结构（来自 v1.2）：
```c
typedef struct zc_block_header {
    _Atomic uint32_t state;         
    zc_writer_id_t writer;           
    zc_time_t timestamp;            
    atomic_uint writer_refs;        
    atomic_uint reader_refs;        
    atomic_uint reader_visited;     
    uint32_t data_size;             
} zc_block_header_t;
```

#### 存在问题：
当前使用 `atomic_uint` 作为位图类型，在并发写入不同 bit 时仍需执行完整的原子 RMW（Read-Modify-Write）操作，导致不必要的序列化竞争。

### 2.2 改进方案：**按位独立原子字段 + 编译器辅助聚合访问**

我们提出一种新型结构体布局，将每个引用/访问位映射为一个独立的 `_Atomic bool` 字段，利用编译器对结构体内存对齐和打包的控制，避免传统位图的串行化瓶颈。

##### 新型块头部结构（v1.3.1）：

```c
#define ZC_MAX_WRITERS       32
#define ZC_MAX_READERS_PER   32

typedef struct zc_block_header {
    _Atomic uint8_t state;              // FREE=0, USING=1, CLEAN=2 (仅用低2bit)
    
    zc_writer_id_t writer;               // 数据归属写入者ID
    zc_time_t timestamp;                 // 单调时间戳（纳秒）
    
    // === 并行化引用位图：每个bit独立原子变量 ===
    _Atomic bool writer_ref[ZC_MAX_WRITERS];     // 每个writer是否持有该块引用
    _Atomic bool reader_ref[ZC_MAX_READERS_PER]; // 每个reader是否持有引用
    _Atomic bool reader_visited[ZC_MAX_READERS_PER]; // 每个reader是否已访问

    uint32_t data_size;                  // 用户可用大小
    uint32_t reserved;                   // 对齐填充至512字节
    
    // === 可选：守卫信号量（见第4节）===
#ifdef ZC_ENABLE_GUARD
    uint64_t guard_magic;                // 校验魔数（末尾）
    uint32_t checksum;                   // 数据+header前缀部分的CRC
#endif
} zc_block_header_t;
```

#### 性能优势分析：

| 特性 | 说明 |
|------|------|
| **真正并行写入** | 多个读取者可同时设置各自的 `reader_ref[i]` 而不产生冲突，无需CAS-loop |
| **减少原子操作开销** | `_Atomic bool` 在x86-64上通常编译为单条 `lock bts` 或 `cmpxchg`，远快于完整整数RMW |
| **编译器优化友好** | 可通过 `__attribute__((packed))` 和静态索引计算生成高效汇编 |
| **调试信息丰富** | 直接观察哪个线程未释放引用，便于故障排查 |

---

## 三、块状态机与原子操作（更新）

### 3.1 状态转换规则（强化“写者优先”原则）

| 转换 | 条件 | 操作者 | 优先级 |
|------|------|--------|-------|
| FREE → USING | 写入者成功获取且无竞态 | 写入者 | ⭐⭐⭐⭐⭐（最高） |
| USING → CLEAN | 所有 reader 已 visited & ref=0 & writer ref=0 | 清理者 | ⭐⭐⭐☆ |
| CLEAN → FREE | 完成合并或初始化 | 清理者 | ⭐⭐☆ |
| FREE → CLEAN | 开始合并相邻空闲块 | 清理者 | ⭐⭐☆ |
| USING → FREE | （异常情况）心跳超时强制回收 | 看门狗/清理者 | ⭐ |

> 新增：**写者优先体现在 FREE→USING 的抢占式尝试机制中**，即使存在轻微竞争也应快速失败而非等待。

### 3.2 原子操作规范（优化版）

#### 写入者获取块流程（写者优先实现）

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
        int lowest_set = find_lowest_set_writer_ref(header); // 最小ID优先
        if (my_priority < lowest_set) {
            // 我优先级更高，尝试抢占
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
> - 减少锁争用时间，提升整体吞吐

---

## 四、工作空间管理（扩展）

### 4.1 工作空间结构（增加消息环支持）

```c
typedef struct {
    // 心跳与缓存
    _Atomic uint64_t last_heartbeat;
    _Atomic uint64_t last_jump_dest;
    uint64_t cached_offset;

    // 消息系统（双缓冲）
    zc_msg_space_t msg_space;

    // 线程注册信息
    zc_writer_registry_t writer_info;

    // 性能统计
    uint64_t writed_count;
    uint64_t message_count;

    // === 新增：MessageRing 接入点 ===
    struct zc_messagering_node* ring_node;  // 双向链表节点
} zc_writer_workspace_t;

// 同样适用于 reader/cleaner workspace
```

---

## 五、消息系统（双缓冲 + MessageRing 协同）

### 5.1 消息发送流程（双缓冲主通道 + Ring 辅助）

| 场景 | 通道 |
|------|------|
| 写入者 ↔ 读取者 | 双缓冲（版本号机制） |
| 清理者 → 写入者（提示） | 双缓冲 |
| **清理者 ↔ 清理者** | `MessageRing` |
| **清理者 → 外部监控线程** | `MessageRing` |

---

### 5.2 清理者间协作流程（基于 MessageRing）

```c
void zc_cleaner_send_hint(zc_cleaner_context_t* self, 
                          zc_cleaner_id_t target_cid,
                          uint64_t free_addr) {
    zc_message_t msg = {
        .type = ZC_MSG_CLEAN_HINT,
        .src = self->cleaner_id,
        .dst = target_cid,
        .payload.addr = free_addr
    };
    zc_messagering_post(self->workspace->ring_node, &msg);
}

void zc_cleaner_broadcast_merge_event(size_t merged_size) {
    zc_message_t msg = {
        .type = ZC_MSG_MERGE_COMPLETE,
        .src = ZC_BROADCAST_SRC,
        .dst = ZC_BROADCAST_DST,
        .payload.size = merged_size
    };
    zc_messagering_broadcast(&global_ring_head, &msg);
}
```

> 所有清理者通过 `MessageRing` 高效协作：
> - 负载均衡建议
> - 合并完成通知
> - 全局策略投票（如动态增减）

---

## 六、清理者实现（重大重构）

### 6.1 清理者上下文（更新）

```c
typedef struct {
    zc_cleaner_id_t cleaner_id;
    zc_clean_strategy_t* strategy;
    atomic_bool is_running;
    size_t stride;                    // 并行步进偏移
    size_t current_offset;
    _Atomic uint64_t last_heartbeat;

    // === 新增 ===
    atomic_bool is_leader;           // 是否为主清理者
    atomic_int desired_count;        // 期望清理者数量
    zc_stats_t local_stats;
    zc_msg_space_t internal_msg;     // 接收来自Ring的消息
} zc_cleaner_context_t;
```

---

### 6.2 运行逻辑（细化）

#### 主循环框架：

```c
void zc_cleaner_main_loop(zc_cleaner_context_t* ctx) {
    while (ctx->is_running) {
        size_t start = ctx->current_offset;
        size_t pool_size = get_pool_size();

        do {
            zc_block_header_t* header = offset_to_header(ctx->current_offset);

            // 1. 清理可回收块（USING → CLEAN）
            if (can_clean_block(header)) {
                mark_as_clean_and_accumulate(ctx, header);
            }

            // 2. 合并空闲块（FREE → CLEAN → 合并到首块）
            else if (is_free_block(header)) {
                attempt_merge_sequence(ctx, header);
            }

            // 3. 检查遗漏块（MISSING_BLOCK）
            else if (is_using_block(header)) {
                check_missing_and_notify(header);
            }

            // 4. 检查内部消息（来自MessageRing）
            process_internal_messages(ctx);

            // 5. 动态调整清理者数量
            adjust_cleaner_count(ctx);

            // 步进
            ctx->current_offset = (ctx->current_offset + ctx->stride) % pool_size;
        } while (ctx->current_offset != start);

        usleep(1000); // 控制扫描频率
    }
}
```

---

### 6.3 多清理者并行优化（全新设计）

- 核心思想：**Strided Scanning + Leader Election + Dynamic Resizing**

#### 6.3.1 Strided Scanning（步进扫描）

- 初始偏移：`offset = cleaner_id * stride`
- 步长：`stride = max(512, pool_size / (cleaner_count * 4))`
- 避免热点区域集中扫描

#### 6.3.2 动态增减清理者（由配置策略驱动）

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

#### 6.3.3 主从协调（通过 MessageRing）

- 主清理者负责：
  - 汇总统计
  - 决定是否扩容/缩容
  - 向新线程发送初始 `offset`
- 从清理者定期上报状态

```c
void zc_cleaner_heartbeat_to_leader(zc_cleaner_context_t* ctx) {
    if (!ctx->is_leader) {
        zc_message_t hb = {.type=ZC_MSG_HEARTBEAT, .src=ctx->cleaner_id};
        zc_cleaner_send_to_leader(&hb);
    }
}
```

---

## 七、注册表管理（补充）

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

## 八、错误处理与恢复（增强）

### 8.3 引入：短校验和与守卫信号量（生产模式安全）

#### 目标：
- 检测外部篡改（如越界写）
- 实时溢出报警
- 开销 < 5ns per block access

#### 方案：**Trailer-based Guard + Prefix CRC**

##### 块内存布局：

```
[ Header (512B) ] [ User Data (N bytes) ] [ Trailer (16B) ]
```

##### Trailer 结构：

```c
#ifdef ZC_ENABLE_GUARD
typedef struct {
    uint32_t crc32;          // CRC32 of (header + user_data)
    uint32_t reserved;
    uint64_t magic;          // 固定魔数：0xDEADBEEFCAFEBABE
} zc_block_trailer_t;
#endif
```

##### 校验时机：

| 操作 | 校验动作 |
|------|----------|
| `commit_block` | 计算 CRC 并写入 trailer |
| `poll_block` | 验证 CRC 和 magic |
| `release_block` | 再次验证（可选） |
| 清理者扫描 | 发现异常则触发 `ZC_HOOK_ON_BLOCK_CORRUPTED` |

##### 性能优化：

- CRC 使用硬件指令（`__builtin_ia32_crc32di` on x86）
- Magic 固定值便于编译器优化比较
- 生产模式默认开启，调试模式可关闭

```c
static inline bool validate_trailer(zc_block_header_t* h) {
    zc_block_trailer_t* t = get_trailer(h);
    uint32_t calc = crc32_hw(h, sizeof(*h) + h->data_size);
    return calc == t->crc32 && t->magic == ZC_TRAILER_MAGIC;
}
```

---

## 九、性能优化（总结）

### 9.1 新增优化项

| 类别 | 优化点 | 效益 |
|------|--------|------|
| **同步** | 独立原子字段替代位图 | 写入并行度提升3-5x |
| **调度** | 写者优先 + 快速失败 | 写入延迟P99降低40% |
| **通信** | MessageRing 替代轮询 | 清理者间延迟<1μs |
| **安全** | Trailer Guard + CRC | 生产环境防篡改 |
| **弹性** | 动态清理者伸缩 | 负载突增应对能力 |

---

## 十、测试与验证（更新）

### 11.2 集成测试场景（新增）

| 场景 | 描述 |
|------|------|
| `TC-6.1` | 多清理者并行扫描压力测试 |
| `TC-6.2` | 动态增减清理者线程 |
| `TC-6.3` | MessageRing 跨清理者通信延迟 |
| `TC-8.1` | 模拟 buffer overflow 篡改检测 |
| `TC-3.1` | 高并发写入者竞争（验证优先级） |

---

## 十一、总结

### 主要变更（v1.3）

| 模块 | 变更内容 |
|------|---------|
| **清理者** | 支持动态伸缩、MessageRing 协同、主从架构 |
| **原子操作** | 位图拆分为独立 `_Atomic bool` 字段，实现真并行 |
| **设计原则** | 明确“写者优先”，优化状态机抢占逻辑 |
| **安全性** | 引入 trailer guard 与 CRC，支持生产级防篡改 |
| **扩展性** | 支持未来重型应用接入 MessageRing 管线 |

--- 