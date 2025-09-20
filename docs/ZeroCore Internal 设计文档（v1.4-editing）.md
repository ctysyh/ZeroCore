# ZeroCore Internal 设计文档（v1.4-editing）

---

## 目录

<!-- TOC -->
- [ZeroCore Internal 设计文档（v1.4-editing）](#zerocore-internal-设计文档v14-editing)
  - [目录](#目录)
  - [一、共享结构](#一共享结构)
    - [1.1 内存池结构](#11-内存池结构)
    - [1.2 内存段](#12-内存段)
    - [1.3 内存页](#13-内存页)
      - [数据结构](#数据结构)
      - [管理逻辑](#管理逻辑)
    - [1.4 内存块](#14-内存块)
      - [数据结构](#数据结构-1)
      - [Header](#header)
      - [块状态机](#块状态机)
    - [1.5 注册表](#15-注册表)
    - [1.6 系统线程简述](#16-系统线程简述)
  - [二、Zora](#二zora)
    - [2.1 `zc_handle_t` 定义](#21-zc_handle_t-定义)
    - [2.2 `zora_thread_cache_t` 数据结构](#22-zora_thread_cache_t-数据结构)
    - [2.3 线程缓存全局数组](#23-线程缓存全局数组)
      - [数据结构：](#数据结构-2)
      - [注册与注销：](#注册与注销)
      - [分配逻辑（伪代码）：](#分配逻辑伪代码)
      - [数据使用](#数据使用)
    - [2.4 密钥管理](#24-密钥管理)
    - [2.5 安全与性能](#25-安全与性能)
  - [三、类型系统](#三类型系统)
    - [3.1 动态类型标记区 (DTTA)](#31-动态类型标记区-dtta)
    - [3.2 类型描述符](#32-类型描述符)
    - [3.3 类型描述符串](#33-类型描述符串)
  - [四、写入者](#四写入者)
    - [4.1 注册与 ID 规则](#41-注册与-id-规则)
    - [4.2 写入者工作空间](#42-写入者工作空间)
    - [4.3 获取块（写者优先路径）](#43-获取块写者优先路径)
    - [4.4 提交与取消](#44-提交与取消)
  - [五、读取者](#五读取者)
    - [5.1 注册与 ID 规则](#51-注册与-id-规则)
    - [5.2 写入者工作空间](#52-写入者工作空间)
    - [5.3 查找未读块（尽力 FIFO）](#53-查找未读块尽力-fifo)
    - [5.4 释放引用](#54-释放引用)
  - [六、清理者及其内部协作](#六清理者及其内部协作)
    - [6.1 角色与生命周期](#61-角色与生命周期)
    - [6.2 清理者工作逻辑](#62-清理者工作逻辑)
    - [6.3 MessageRing 内部消息](#63-messagering-内部消息)
    - [6.4 运行时动态优化](#64-运行时动态优化)
  - [七、主消息通道](#七主消息通道)
    - [7.1 数据结构](#71-数据结构)
    - [7.2 消息接口](#72-消息接口)
      - [概述和接口规范：](#概述和接口规范)
      - [消息发送接口：](#消息发送接口)
      - [消息接收接口](#消息接收接口)
      - [消息转发函数](#消息转发函数)
  - [八、跨平台抽象](#八跨平台抽象)
  - [九、公共 API 内部逻辑](#九公共-api-内部逻辑)
    - [9.1 系统 API 内部逻辑](#91-系统-api-内部逻辑)
    - [9.2 Zora API 内部逻辑](#92-zora-api-内部逻辑)
  - [十、错误码](#十错误码)
    - [10.1 错误码 `zc_internal_result_t`](#101-错误码-zc_internal_result_t)
  - [十一、 测试用例与指标](#十一-测试用例与指标)

<!-- /TOC -->

---

## 一、共享结构

### 1.1 内存池结构

```c
typedef struct {
    char* name;                   // 名称
    zc_segment_t* segments;       // 池内存段
    atomic_size_t segment_count;  // 内存段数量
    zc_stats_t stats;             // 全局统计
    zc_registry_t registry;       // 注册表

    // === 内部线程资源 ===

} zc_memory_pool_t;
```

- 分配方式：初始化时池本体 lazy alloc 直到有第一个写入者注册，运行时可以通过增加/减少内存段动态扩缩容。
- 生命周期：`zc_init()` 创建，`zc_destroy()` 归还给 OS。

### 1.2 内存段

<TODO> **包括元数据、运行时新增和释放、排序优化、NUMA/跨节点感知优化等**

### 1.3 内存页

#### 数据结构

```c
typedef struct zc_page_header {
    uint64_t line_seq : 61;   // 行序号
    uint64_t state    : 3;    // 状态标识
    uint64_t prev_page_addr;  // 前一页物理地址
} zc_page_header_t;

typedef struct zc_page_tail {
    uint64_t next_page_addr;  // 下一页物理地址
} zc_page_tail_t;
```

- 整体大小为 512 字节，头尾之间的主要部分为数据承载区。

#### 管理逻辑

<TODO> **包括内部线程所需的统计数据、排序优化、邻页预取硬件提示等**

### 1.4 内存块

#### 数据结构

| 区域 | 大小 | 说明 |
|------|------|------|
| Header | 64 B | 含状态、ID、引用位图、时间戳、DTTA条目数 |
| UserData | 用户请求长度 | 返回给用户的指针即此区域首地址 |
| DTTA | 动态可变 | 动态类型标记区（Dynamic Type Tag Area） |
| Padding | 0 ~ 424 B | 使总和为 488 B 的倍数 |

#### Header

```c
typedef struct zc_block_header {
    _Atomic uint16_t  state;           // FREE=0, USING=1, CLEAN=2
    uint16_t          reserved_flags;  // 未来扩展位
    uint32_t          writer_id;       // 写入者 ID
    zc_time_t         timestamp;       // 写入时间戳
    uint64_t          lut_offset;      // DTTA 查找表偏移量
    uint64_t          lut_entry_count; // DTTA 条目数量

    // === 并行引用位图===
    bool      writer_ref[ZC_MAX_WRITERS];         // 写入者实时引用
    bool      reader_ref[ZC_MAX_READERS_PER];     // 读取者实时引用
    bool      reader_visited[ZC_MAX_READERS_PER]; // 读取者访问历史
} zc_block_header_t;
```

#### 块状态机

| 状态值 | 名称 | 含义 |
|--------|------|------|
| 0 | FREE | 空闲可分配或正在被争夺 |
| 1 | USING | 写入者已提交，读取者可消费 |
| 2 | CLEAN | 正在清理或合并 |

合法转换（只有以下 4 种）：

1. FREE → USING：写入者成功获取（写者优先，CAS 状态即可）。
2. FREE → CLEAN：清理者开始合并相邻 FREE 块。
3. USING → CLEAN：清理者确认 reader_visited 中（除了异常者或者自主退出者之外）所有需要读取的读取者对应位为 1 且所有引用位图为 0
4. CLEAN → FREE：清理者完成合并，把长度累加到前驱块。

### 1.5 注册表

```c
typedef struct {
    zc_writer_registry_t   writers[ZC_MAX_WRITERS];
    zc_reader_registry_t   readers[ZC_MAX_WRITERS * ZC_MAX_READERS_PER];
    zc_cleaner_context_t   cleaners[ZC_MAX_CLEANERS];
    atomic_flag            lock; // 仅用于注册/注销临界区
} zc_registry_t;
```

- 读写分离：注册/注销需拿 `lock`；查询（遍历时）无锁，读端使用 `atomic_load(&count)` 快照。
- 故障清理：看门狗线程每 100 ms 检查心跳，超时即强制清 `writer_ref` / `reader_ref` 并更新其元数据中的状态信息。

### 1.6 系统线程简述

<TODO>

---

## 二、Zora

- 引入 Zora 安全访问器，提供更高效、更安全的内存访问控制。

### 2.1 `zc_handle_t` 定义

64位加密句柄：
```c
typedef struct zc_handle {
    uint64_t address : 60;
    uint64_t version : 4;
} zc_handle_t;
```

- 高4位：handle 版本号，用于防重放攻击。
- 低60位：使用当前线程密钥加密后的块 Header 虚拟地址。

### 2.2 `zora_thread_cache_t` 数据结构

每个注册的外部线程（写入者/读取者）拥有一个线程本地缓存：

```c
typedef struct zora_thread_cache {
    uint64_t  encryption_key;   // 当前线程加密密钥
    int16_t   thread_state;     // 当前线程状态
    uint16_t  current_version;  // 当前版本号
    uint32_t  padding;
    void*     workspace;        // 指向线程工作空间 (zc_writer_workspace_t / zc_reader_workspace_t)
    union     thread_id {
        zc_writer_id_t writer_id;
        zc_reader_id_t reader_id;
    };                          // 线程 ID
} zora_thread_cache_t;
```

- 每次访问线程缓存时检查 API 传入参数中的 ID 和线程缓存中的 thread_id 是否匹配，防止传入假 ID。

### 2.3 线程缓存全局数组

#### 数据结构：

```c
static zora_thread_cache_t* g_thread_cache_pool[ZC_MAX_WRITERS][ZC_MAX_READERS_PER + 1];
```

- g_thread_cache_pool 作为 zora.c 文件范围内的静态数组。

#### 注册与注销：

- 外部线程调用 `zc_writer_register` 或 `zc_reader_register` 时触发注册。
- 线程调用 zc_writer_unregister` 或 `zc_reader_unregister` 时释放指向的内容，数组中的对应项清零。

#### 分配逻辑（伪代码）：

```c
// <TODO> **函数签名需要修改，实际 tid 会因为区分写入者和读取者而位数不同**
static zora_thread_cache_t* zora_ensure_thread_registered(uint32_t tid)
{
    zora_thread_cache_t* cache = g_thread_cache_pool[tid];
    if (cache) return NULL; // 重复注册

    // 加轻量自旋锁
    while (__atomic_exchange_n(&g_registration_lock, 1, __ATOMIC_ACQUIRE))
        __builtin_ia32_pause();

    // 双重检查
    cache = g_thread_cache_pool[tid];
    if (!cache)
    {
        cache = _zora_create_tls_cache_for_current_thread(); // 获取当前线程 TLS 块
        if (cache)
        {
            cache->owner_tid = tid;
            cache->magic = ZORA_CACHE_MAGIC;
            __atomic_store_n(&g_thread_cache_pool[tid], cache, __ATOMIC_RELEASE);
        }
    }

    __atomic_store_n(&g_registration_lock, 0, __ATOMIC_RELEASE);
    return cache;
}

static inline zora_thread_cache_t* _zora_create_tls_cache_for_current_thread(void)
{
    static __thread zora_thread_cache_t s_tls_cache = {0};
    if (__atomic_load_n(&s_tls_cache.magic, __ATOMIC_RELAXED) != ZORA_CACHE_MAGIC)
    {
        zora_thread_cache_t* cache = &s_tls_cache;
        cache->encryption_key = 0; // <TODO> **这里应该调用随机数生成器**
        cache->current_version = 0;
        cache->workspace = NULL; // <TODO> **这里应该修改成正确的工作空间地址。需要考虑是先初始化这个线程缓存，还是先初始化线程工作空间，从而决定这里是应该增加参数传入工作空间地址，还是调用工作空间的初始化函数**
        cache->thread_state = 0;
        cache->owner_tid = (uint32_t)syscall(SYS_gettid);
    }
    return &s_tls_cache;
}
```

#### 数据使用

```c
static inline zora_thread_cache_t* zora_get_cache_writer(zc_writer_id_t id)
{
    return g_thread_cache_pool[id][0];
}

static inline zora_thread_cache_t* zora_get_cache_reader(zc_reader_id_t id)
{
    return g_thread_cache_pool[(uint32_t)(id >> 32)][(uint32_t)id + 1];
}
```

### 2.4 密钥管理

- 生成：由系统线程（清理者）定时轮换（如每5秒）或事件触发，使用硬件随机数生成器。
- 分发：通过工作空间内的主消息管线，在 `zc_writer_commit_block` / `zc_writer_cancel_block` / `zc_reader_release_block` 时更新。
- 使用：存储于线程本地缓存，用于句柄的加解密。

### 2.5 安全与性能

- 防重放：版本号机制确保旧句柄在块提交/取消后失效。
- 防泄露：句柄中无明文地址，密钥不暴露给用户。
- 高性能：TLS + 轻量级 XOR 加解密，解析延迟 ≤ 3 cycles。

---

## 三、类型系统

### 3.1 动态类型标记区 (DTTA)

- 位置：紧随 `UserData` 区域之后。
- 结构：由元数据查询表（LUT）和类型描述符串组成。
    - LUT 表项：
      ```c
      typedef struct zc_dtt_lut_entry {
          uint64_t data_offset;  // UserData 中变量起始偏移
          uint64_t desc_offset;  // 类型描述符在 DTTA 中的起始偏移
      } zc_dtt_lut_entry_t;
      ```

### 3.2 类型描述符

- 采用 ECMA-335 CLI 规范的 `CorElementType` 枚举（私有解释），支持附加 tokens。具体定义和解释参见 Zora 使用文档： 1.1 类型描述符。

### 3.3 类型描述符串

- 行为：<TODO> 剩余长度不足时的跨页机制

---

## 四、写入者

### 4.1 注册与 ID 规则

```c
ZC_API zc_result_t zc_writer_register(
    const zc_thread_config_t* conf,
    zc_writer_id_t* out_id
);
```

- ID 分配：静态数组索引 0~31，先到先得。
- 伴随动作：
  - 创建 `zc_writer_workspace_t`。

### 4.2 写入者工作空间

```c
typedef struct zc_writer_workspace {
    // === 状态标识 ===
    _Atomic uint32_t     state;
    uint32_t             reserved;

    // === 心跳与缓存 ===
    uint64_t     last_heartbeat;
    uint64_t     last_jump_dest;
    uint64_t             cached_block;

    // === 线程注册信息 ===
    zc_writer_registry_t writer_info;

    // === 消息系统 ===
    zc_rwer_msg_space_t  msg_space;

    // === 性能统计 ===
    uint64_t             writed_count;
    uint64_t             message_count;

    // === Zora 线程缓存指针 ===
    zora_thread_cache_t* zora_cache; // 指向该写入者线程的 Zora 缓存
} zc_writer_workspace_t;
```

### 4.3 获取块（写者优先路径）

```c
ZC_API zc_result_t zc_writer_acquire_block(
    zc_writer_id_t writer_id,
    size_t size,
    zc_handle_t* out_handle,
    uint64_t timeout_ns);
```

内部逻辑：
- 按策略挑候选块（先 cached_block 的后向邻接块 → 再 guide 地址 → 全局遍历）。
- 挑选逻辑：
  - 检查 `state == FREE && 无任意引用位被置 1`。
  - 检查块的可用大小不小于参数中要求的 size。
  - 置 `writer_ref[idx] = true`。
  - CAS `state FREE→FREE` 防止与清理者的竞态。
  - 检查 `writer_ref 中比自己靠前的位全 false`。
  - 成功则确认获取当前块。
  - 更新 `cached_block` 并将旧 `cached_block` 指向的 header 中的 `writer_ref[idx]` 置为 `false`。
  - 判断当前块的可用大小减去自己要求的 size 之后剩余量是否大于 512 B，如果是则需要将剩余部分新增 header 标记可用块。
  - 填写时间戳（调用 `zc_timestamp()`）。
  - 生成句柄：使用 `zora_cache` 中的密钥和版本号，将块 Header 的虚拟地址加密，生成 `zc_handle_t`。
  - 初始化 DTTA：`lut_entry_count` 初始化为 0，UserData 区域默认视为 `ZC_TYPE_UNTYPED` (0x5C)。
  - 返回 `out_handle`。
- 失败处理：
  - 立即放弃，寻找其他块。
- 超时机制：
  - 每次检查块后检查耗时，硬截止 `timeout_ns`，到点返回 `ZC_ERROR_TIMEOUT`。

### 4.4 提交与取消

```c
ZC_API zc_result_t zc_writer_commit_block(
    zc_writer_id_t writer_id
);
```

- 填写时间戳（调用 `zc_timestamp()`）。
- 原子置 `state = USING`。
- 冻结 DTTA：提交后，DTTA 内容不可再被修改。
- 更新密钥：检查并应用系统线程分发的新密钥，并 ++ 版本号。

```c
ZC_API zc_result_t zc_writer_cancel_block(
    zc_writer_id_t writer_id
);
```

- 仅填写时间戳（调用 `zc_timestamp()`）。
- 更新密钥：检查并应用系统线程分发的新密钥，并 ++ 版本号。

---

## 五、读取者

### 5.1 注册与 ID 规则

```c
ZC_API zc_result_t zc_reader_register(
    zc_writer_t target_writer,
    zc_reader_id_t* out_id
);
```

- ID 高 32 位 = `writer_id`，低 32 位 = 自增序号 → 至多 32 读者 per 写入者。

### 5.2 写入者工作空间

```c
typedef struct zc_reader_workspace {
    // === 状态标识 ===
    _Atomic uint32_t state;
    uint32_t reserved;

    // === 心跳与缓存 ===
    uint64_t last_heartbeat;
    uint64_t cached_block;

    // === 线程注册信息 ===
    zc_reader_registry_t reader_info;

    // === 消息系统 ===
    zc_rwer_msg_space_t msg_space;

    // === 性能统计 ===
    uint64_t read_count;
    uint64_t message_count;

    // === Zora 线程缓存指针 ===
    zora_thread_cache_t* zora_cache; // 指向该读取者线程的 Zora 缓存
} zc_reader_workspace_t;
```

### 5.3 查找未读块（尽力 FIFO）

```c
ZC_API zc_result_t zc_reader_poll_block(
    zc_reader_id_t reader_id,
    size_t hint_size,
    zc_handle_t* out_handle,
    uint64_t timeout_ns);
```

内部逻辑：
- 从 `cached_block` 开始向后线性扫描。
- 匹配条件：
  - `state == USING`
  - `writer_id` 与注册目标一致
  - `reader_visited[reader_idx] == false`
- 当读取到不匹配的块，访问对应写入者的 `LastJumpDest`，只检测对应块不向后线性扫描，仍不匹配从头遍历。
- 置 `reader_ref[idx] = true` 与 `reader_visited[idx] = true`。
- 更新 `cached_block`，释放对旧缓存块的 `reader_ref[idx]`。
- 生成句柄：使用 `zora_cache` 中的密钥和版本号，将块 Header 的虚拟地址加密，生成 `zc_handle_t`。
- 返回 `out_handle`。
- 检查超时，否则持续遍历。

### 5.4 释放引用

```c
ZC_API zc_result_t zc_reader_release_block(
    zc_reader_id_t reader_id
);
```

- 更新密钥：检查并应用系统线程分发的新密钥，并 ++ 版本号。

---

## 六、清理者及其内部协作

<TODO> **本节内容需要大幅调整，匹配重新定义的链表结构内存池和各项新任务**

### 6.1 角色与生命周期

- 默认至少 1 个清理者，可动态增至 `cleaner_max_count`。
- 新增/减少清理者线程根据负载率投票决定、由当值者执行。
- 每个清理者拥有独立 `zc_cleaner_context`，共享 `zc_cleaner_workspace`。

```c
typedef struct zc_cleaner_workspace {
    // === 消息系统 ===
    zc_rwer_msg_space_t msg_space_for_writers[ZC_MAX_WRITERS];
    zc_rwer_msg_space_t msg_space_for_readers[ZC_MAX_WRITERS][ZC_MAX_READERS_PER];

    // === 指导地址 ===
    zc_free_block_guidance blk_guide[ZC_MAX_WRITERS];

    // === 性能统计 ===
    uint64_t clean_count;
    uint64_t message_count;
} zc_cleaner_workspace_t;
```

### 6.2 清理者工作逻辑

- 遍历：从池首开始，根据每个 header 按块遍历内存池。
- 清理：识别可清理块（USING 状态、所有需访问的读取者已访问、引用位图全零），将其状态更新为 CLEAN、清零读取者访问记录位图；然后继续遍历直到遇到不可清理的块，然后将中间经过的块的长度（包括header）累加更新到连续序列的第一个块 header 中。
- 合并：识别空闲块（FREE 状态、位图全零）并缓存其偏移量；继续遍历，如果遇到的是一个新的空闲块，则将其标识 CAS 为 CLEAN 并累加其 size（包括 header），直到遇到非空闲块或 CAS 失败；然后再次检查缓存的块（连续序列的第一个块）是否仍然空闲，如果空闲则将其 data_size 原子地加上累计的后续 size，如果不空闲则只修改其后块（连续序列的第二个块）的 data_size 为累计的后续 size - HEADER_SIZE。
- 未读通知：遍历到 USING 状态的块时检查其读取者访问记录位图是否与预期的“所有读取者均已访问”的状态一致，如果不一致则检查其时间戳与当前时间是否达到间隔阈值，如果达到则需检查未完成读取的读取者心跳并向其发送 ZC_MSG_MISSING_BLOCK 消息。
- 空闲块提示：根据 zc_alloc_strategy 中定义的逻辑，对于例如长度足够且与前置内存块的写入者一致、或者长度过长可以分割等的空闲块，将其偏移量提供给所有或特定的writer，推荐它们在无法保持内存块使用连续性时跳转使用。
- 统计与背压处理：遍历过程中累计相关信息，根据 zc_backpressure_strategy 中定义的逻辑计算出负载率，在负载率达到阈值时通知写入者和读取者、动态扩缩内存池等。
- 密钥轮换：根据定时器或事件触发，生成新密钥，并通过消息系统通知所有活跃的写入者和读取者，不同外部线程的密钥应当不同。

### 6.3 MessageRing 内部消息

- 环线结构：双向链表节点，每节点 = 一个清理者。
- 消息类型：

| 消息码 | 含义 |
|--------|------|
| MSG_MERGE_REQ | 请求邻居暂停并协助合并 |
| MSG_SCALE_UP | 投票增加清理者 |
| MSG_SCALE_DOWN | 投票减少清理者 |

- 传递方式：
  - 常规：单向单包，指针沿环转发，命中 dst 即处理。
  - 关键：双向双包，确保至少一个方向送达。
- 当值者简介：首个发送的包带 `ownership_token=1`，收到者即为当值；超时未心跳则由守护者线程随机指定新当值者并广播。
- 接口：
  - 注册节点，包括元数据和一系列与消息类型对应的事件驱动的函数指针。
  - on_message()，内部自动完成所有消息检查、处理、转发和当值着身份传递逻辑，调用注册的函数指针处理对应类型的消息，更新心跳。
  - 注销节点。

### 6.4 运行时动态优化

- 动态扩缩容：扩容和缩容都以子内存段为单位，内存池在创建时即可划分子内存段（如配合 NUMA 优化），扩容时先准备好新的内存段，然后更新指针翻译器，最后锁定内存池末块（标记为 CLEAN）、更新内存池的元数据和末块的 header。
- 动态多清理者步进偏移：以相隔时间而非相隔偏移量为均衡指标，使得整体各个内存块的“最大清理者扫描时延”最小化。

---

## 七、主消息通道

### 7.1 数据结构

- 主消息通道采用双缓冲设计，用于外部工作线程（写入者和读取者）与系统线程（清理者）之间的通信。
- 消息头格式：

```c
typedef struct zc_msg_header {
    uint16_t         version;      // 版本号
    uint8_t          type;         // zc_message_type_t
    uint8_t          length;       // payload 长度（字节）
    zc_short_time_t  create_time;  // 创建时间
} zc_msg_header_t;
```

  - 消息头后紧邻 payload，多个消息在缓冲区内连续追加，形成紧凑的消息流。

- 消息元数据窗口数据结构：

```c
typedef struct zc_rwer_msg_space {
    _Atomic uint16_t  read_version;     // 自己作为接收方，已读取的最新版本
    _Atomic uint16_t  write_version;    // 自己作为发送方，活动缓冲区的版本
    atomic_bool       buffer_swapping;  // 缓冲区切换中标志
    atomic_bool       is_accumulating;  // 是否有积累的消息
    atomic_uintptr_t  active_buffer;    // 当前活动缓冲区
    atomic_size_t     msg_count;        // 活动缓冲区中的消息数量
    atomic_uintptr_t  standby_buffer;   // 备用缓冲区
} zc_rwer_msg_space_t;

// #define RWER_MS_SIZE 32
```

  - 此结构体是外部线程工作空间的一部分；清理者工作空间包含zc_rwer_msg_space_t[]数组，与每个外部线程维护一一对应的独立窗口。
  - standby_buffer 不应该被接收方访问，其有效性和安全性在运行时不做任何保证，仅供发送方缓存使用。

### 7.2 消息接口

#### 概述和接口规范：

- 对外暴露的接口仅有消息发送 API，其余接口均为内部接口。
- 由外部线程的其他 API 在内部逻辑中调用消息检查接口，且仅在外部线程不正在持有 handle 时调用消息检查接口，防止因执行通过这个消息通道传递的设置更新带来不一致性。

#### 消息发送接口：

```c
ZC_API zc_result_t zc_writer_send_message(
    zc_writer_id_t writer_id,
    zc_message_type_t type,
    uint16_t length,
    const void* msg
);

ZC_API zc_result_t zc_reader_send_message(
    zc_reader_id_t reader_id,
    zc_message_type_t type,
    uint16_t length,
    const void* msg
);

inline zc_internal_result_t zc_cleaner_send_message_to_writer(
    zc_writer_id_t writer_id,
    zc_message_type_t type,
    uint64_t content
);

inline zc_internal_result_t zc_cleaner_send_message_to_reader(
    zc_reader_id_t reader_id,
    zc_message_type_t type,
    uint64_t content
);
```

- 内部逻辑：
  - 将消息追加写入备用缓冲区。
  - 访问接收方的工作空间，检查其读取版本是否等于当前活动缓冲区版本。
  - 如果相等，将备用缓冲区切换为活动缓冲区，换下来的缓冲区可复用/释放。
  - 如果不相等，将is_accumulating置为true。
  - is_accumulating为true时，触发定期检查接收方读取版本的逻辑，一旦检测到接收方读取版本等于活动缓冲区版本，触发原子切换。
  - 对于清理者（系统线程），只由当值者调用。

#### 消息接收接口

```c
inline zc_internal_result_t zc_writer_check_message(
    zc_writer_id_t writer_id,
    size_t* receive_number,
    size_t* unprocessed_number,
    void* unprocessed_msg_stream
);

inline zc_internal_result_t zc_reader_check_message(
    zc_reader_id_t reader_id,
    size_t* receive_number,
    size_t* unprocessed_number,
    void* unprocessed_msg_stream
);

inline zc_internal_result_t zc_cleaner_check_message(void);
```

- 内部逻辑：
  - 比较本地读取版本缓存与发送方的活动缓冲区的版本。
  - 如果不相等，读取新消息。
  - 根据消息类型查找是否有已注册的处理函数指针，（对于外部线程）如果没有则将消息内容复制并通过参数传回，或者（对于内部线程）使用注册的 `DEFAULT` 处理函数
  - 处理完成后原子更新本地读取版本缓存。

#### 消息转发函数

```c
inline void zc_cleaner_transpond_message_to_writer(
    zc_writer_id_t writer_id,
    char* content,
    uint8_t length
);

inline void zc_cleaner_transpond_message_to_reader(
    zc_reader_id_t reader_id,
    char* content,
    uint8_t length
);
```

- 内部逻辑：
  - 当清理者检测到一个类型为 `REQ_TRANSPOND` 的消息时，调用这两个函数。
  - 转发消息的消息头中 `create_time` 继承原消息。

---

## 八、跨平台抽象

<TODO>

---

## 九、公共 API 内部逻辑

- 详细的 API 列表参见 ZeroCore API Reference Manual

### 9.1 系统 API 内部逻辑

### 9.2 Zora API 内部逻辑

---

## 十、错误码

### 10.1 错误码 `zc_internal_result_t`

<TODO>

---

## 十一、 测试用例与指标

<TODO>

---

*ZeroCore —— “零”开销共享内存引擎*