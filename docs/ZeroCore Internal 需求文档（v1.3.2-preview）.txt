# ZeroCore Internal 需求文档（v1.3.2-preview）  

---

## 一、概述

本文档在 `ZeroCore Internal 需求文档（v1.3-preview）` 基础上，结合 **v1.3.2 更新事项** 进行关键性重构：

- **MessageRing 机制全面落地**：双向链表结构、当值者选举、消息路由策略、嵌套支持
- **安全机制分层设计**：阻断型信号量守卫 + 可选 CRC + 固定魔数兜底
- **内存块头部极致瘦身**：取消 512 字节强制对齐，优化为 64 字节倍数，提升缓存密度
- **块布局重新规划**：Header + UserData + Trailer + Padding → 整体对齐 512 字节

所有改进继续遵循 **无锁、低延迟、写者优先、生产安全** 四大核心原则。

---

## 二、内存管理

### 2.1 块头部结构（v1.3.2）

#### 设计目标：
- 支持独立原子位操作
- 保留状态机、时间戳、引用追踪能力

#### 结构定义：

```c
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

---

## 三、安全机制（分层实现）

### 3.1 块尾校验与魔数

| 层级 | 机制 | 触发条件 | 适用场景 |
|------|------|----------|----------|
| L1 | `guard_signal` | 详见v1.3.3更新 | 默认开启 |
| L2 | `checksum` 校验 | 外部线程显式触发 / 调试模式 | 可选开启 |

> guard_signal 的具体设计参见v1.3.3更新

---

## 四、MessageRing 机制（全面落地）

### 4.1 MessageRing 核心架构

#### 双向链表节点结构：

```c
typedef struct zc_messagering_node {
    struct zc_messagering_node* prev;
    struct zc_messagering_node* next;
    
    // 左右缓冲区（由邻居写入，自己读取）
    zc_message_t* left_buffer;   // ← 左邻居写入
    zc_message_t* right_buffer;  // → 右邻居写入
    
    // 当值者状态
    _Atomic bool is_current_owner;
    _Atomic uint64_t ownership_token; // 跟随消息包传递的令牌

    // 心跳与超时
    _Atomic uint64_t last_heartbeat;
    struct zc_messagering* ring; // 所属环线
} zc_messagering_node_t;
```

---

### 4.2 消息包结构与路由

#### 消息包定义：

```c
typedef struct zc_message {
    uint32_t type;               // ZC_MSG_*
    uint32_t flags;              // UNI/BIDI, PERSIST, URGENT
    zc_cleaner_id_t src;         // 发送者
    zc_cleaner_id_t dst;         // 目标（0=广播，>0=定向）
    uint64_t deadline_ns;        // DDL（0=无限制）
    void* payload_ptr;           // 数据指针（传递引用）
    size_t payload_size;
    uint64_t ownership_token;    // 当值者令牌（如携带）
} zc_message_t;
```

#### 两种发送模式：

| 模式 | 描述 | 适用场景 |
|------|------|----------|
| **单向单包** | 选择距离更近方向发送，包头含 dst，沿途节点检查 dst，命中则处理+继续传递，回源/超时/处理完后释放 | 常规通知、负载提示 |
| **双向双包** | 向左右同时发送相同包，逻辑同上，确保至少一个方向送达 | 关键指令、合并确认、主从选举 |

> 消息传递均为指针传递，零拷贝。

---

### 4.3 当值者机制（Owner-Slot）

#### 选举逻辑：

1. **初始化**：环线创建时，第一个发送的消息包携带 `ownership_token = 1`，接收者成为当值者。
2. **传递**：当值者发送新消息时，将 `ownership_token` 注入包中，接收者成为新当值者。
3. **保持**：若当值者无消息发送，保留身份直到超时（默认 100ms）。
4. **超时接管**：守护者线程检测到超时，随机指定新当值者并广播 `MSG_OWNER_TRANSFER`。

#### 当值者职责：

- 管理 **清理者共享工作空间**（如全局统计、合并策略、伸缩决策）
- 代表环线与 **外部系统通信**（如监控线程、重型应用代理）
- 发起 **全池调整投票**（如块大小重配、策略切换）

---

### 4.4 内部消息体系（枚举设计）

```c
typedef enum {
    ZC_MSG_CLEAN_HINT = 1,        // 提示某地址可清理
    ZC_MSG_MERGE_COMPLETE,        // 合并完成通知
    ZC_MSG_HEARTBEAT,             // 心跳上报
    ZC_MSG_SCALE_REQUEST,         // 请求扩容/缩容
    ZC_MSG_VOTE_POOL_RESIZE,      // 投票：调整池大小
    ZC_MSG_VOTE_STRATEGY_CHANGE,  // 投票：切换清理策略
    ZC_MSG_OWNER_TRANSFER,        // 当值者移交
    ZC_MSG_GUARD_TRIP,            // 触发守卫熔断（紧急）
    ZC_MSG_CRC_VERIFY_REQUEST,    // 请求校验某块
} zc_message_type_t;
```

> 所有消息通过 MessageRing 传递，实现清理者间零锁协作。

---

### 4.5 嵌套 MessageRing 支持

```c
typedef struct zc_messagering {
    zc_messagering_node_t* head;
    size_t node_count;
    _Atomic zc_messagering_node_t* current_owner;
    struct zc_messagering* parent_ring; // 可为空
    void* context;                      // 如指向 cleaner_pool
} zc_messagering_t;
```

> 嵌套 MessageRing 中每个节点是子环线，实现层级化通信（如 NUMA 节点间协调）。

---

## 五、清理者实现（v1.3.2 升级）

### 5.1 清理者上下文（接入 MessageRing）

```c
typedef struct {
    zc_cleaner_id_t cleaner_id;
    zc_clean_strategy_t* strategy;
    atomic_bool is_running;
    size_t stride;
    size_t current_offset;
    _Atomic uint64_t last_heartbeat;

    // MessageRing 集成
    zc_messagering_node_t* ring_node;
    _Atomic bool is_leader;       // 是否为当值者（动态）
    zc_stats_t local_stats;

    // 共享工作空间指针（由当值者维护）
    zc_cleaner_shared_workspace_t* shared_ws;
} zc_cleaner_context_t;
```

---

### 5.2 当值者管理共享工作空间

```c
typedef struct {
    _Atomic size_t total_cleaned_bytes;
    _Atomic size_t merge_count;
    _Atomic zc_clean_strategy_id_t current_strategy;
    _Atomic uint64_t last_pool_resize;
    // 可扩展更多全局状态...
} zc_cleaner_shared_workspace_t;
```

> **只有当值者可写**，其他清理者只读，通过 MessageRing 同步变更。

---

### 5.3 清理者主循环（增强版）

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

---

## 六、测试与验证（v1.3.2 新增）

### 6.1 新增测试场景

| 编号 | 场景 | 目标 |
|------|------|------|
| TC-RING-1 | 单向/双向消息传递正确性 | 验证路由逻辑 |
| TC-RING-2 | 当值者超时移交 | 验证高可用 |
| TC-RING-3 | 嵌套环线通信 | 验证扩展性 |
| TC-CRC-1 | 按需触发CRC校验 | 验证可选安全层 |

---

## 七、总结

### 主要变更（v1.3.2）

在系统内部引入**MessageRing**，双向链表+当值者机制全面落地，支持清理者动态协同与共享状态管理。

---