# ZeroCore Internal 需求文档（v1.4.3-preview）

> 核心目标：设计并实现名为 Zora 的安全访问器，通过加密句柄 `zc_handle_t` 与线程本地缓存机制，在极低开销下实现内存地址保护、防重放攻击、防越权访问。
> 威胁模型：确保外部线程无法伪造 ID，也无法构造合法句柄访问有效数据。
> 性能目标：句柄解析延迟 ≤ 3 cycles，线程缓存访问无锁、无共享、无 false sharing。

---

## 一、`zc_handle_t` 定义与算法

### 结构（64位）：

```
| 4-bit Version |          60-bit Encrypted Virtual Address         |
<--------------------------- 64 bits --------------------------->
```

- 高4位：密钥版本号（`current_version`），用于检测过期句柄。每当调用成功一次申请块 API（`zc_writer_acquire_block` 或 `zc_reader_poll_block`）时 `current_version++`（模 16）。
- 低60位：使用当前线程密钥加密后的虚拟地址。地址保留60位，保证对57位地址空间的支持。这里的地址是一个块 header 的地址，具体读写由 offset 进一步指定。

### 加密/解密算法（轻量级 XOR）：

```c
// 加密
zc_handle_t handle = ((uint64_t)version << 60) | (va & 0x0FFFFFFFFFFFFFFFULL);
handle ^= encryption_key;

// 解密 + 校验
uint64_t decrypted = handle ^ encryption_key;
uint8_t version = (decrypted >> 60) & 0x0F;
uint64_t va = decrypted & 0x0FFFFFFFFFFFFFFFULL;
if (version != expected_version) → 失败；
```

> 安全性：地址和版本均加密，密钥定期轮换，攻击者无法从句柄推断真实地址或构造有效句柄。

---

## 二、密钥生命周期管理

### 生成：

- 责任方：系统线程（清理者）。
- 频率：定时轮换（如每5秒）或事件触发（如可疑访问）。
- 算法：使用硬件随机数生成器（如 `rdrand`）或加密安全 PRNG 生成 64 位密钥。

### 分发：

- 通道：工作空间内的主消息管线。
- 时机：外部线程在调用 `zc_writer_commit_block` 或 `zc_writer_cancel_block` 时，API 内部检查若有新密钥则更新。

### 使用：

- 存储于 `zora_thread_cache_t.encryption_key`。
- 用于句柄加解密。

---

## 三、`zora_thread_cache_t` 数据结构

```c
typedef struct zora_thread_cache {
    uint64_t  encryption_key;   // 当前加密密钥
    int16_t   thread_state;     // 当前线程状态，非活动/访问块中/异常/...
    uint16_t  current_version;  // 当前版本号
    uint32_t  padding;
    void*     workspace;        // 工作空间
    union {
              zc_writer_id_t;
              zc_reader_id_t;
    }                           // 线程 ID
} zora_thread_cache_t;
```

---

## 四、线程缓存注册与分配逻辑

### 全局数组：

```c
static zora_thread_cache_t* g_thread_cache_pool[ZC_MAX_WRITERS][ZC_MAX_READERS_PER + 1];
```

### 注册时机：

- 外部线程调用 `zc_writer_register` 或 `zc_reader_register` 时触发。

### 分配逻辑（伪代码）：

```c
// 函数签名可能需要修改，实际 tid 会因为区分写入者和读取者而位数不同
static zora_thread_cache_t* zora_ensure_thread_registered(uint32_t tid) {
    if (tid >= MAX_THREAD_COUNT) return NULL; // 非法tid

    zora_thread_cache_t* cache = g_thread_cache_pool[tid];
    if (cache) return NULL; // 重复注册

    // 加轻量自旋锁
    while (__atomic_exchange_n(&g_registration_lock, 1, __ATOMIC_ACQUIRE)) {
        __builtin_ia32_pause();
    }

    // 双重检查
    cache = g_thread_cache_pool[tid];
    if (!cache) {
        cache = _zora_create_tls_cache_for_current_thread(); // 获取当前线程 TLS 块
        if (cache) {
            cache->owner_tid = tid;
            cache->magic = ZORA_CACHE_MAGIC;
            __atomic_store_n(&g_thread_cache_pool[tid], cache, __ATOMIC_RELEASE);
        }
    }

    __atomic_store_n(&g_registration_lock, 0, __ATOMIC_RELEASE);
    return cache;
}

static inline zora_thread_cache_t* _zora_create_tls_cache_for_current_thread(void) {
    static __thread zora_thread_cache_t s_tls_cache = {0};
    if (__atomic_load_n(&s_tls_cache.magic, __ATOMIC_RELAXED) != ZORA_CACHE_MAGIC) {
        zora_thread_cache_t* cache = &s_tls_cache;
        cache->encryption_key = 0; // 这里应该调用随机数生成器
        cache->current_version = 0;
        cache->workspace = NULL; // 这里应该修改成正确的工作空间地址，**需要考虑是先初始化这个线程缓存，还是先初始化线程工作空间，从而决定这里是应该增加参数传入工作空间地址，还是调用工作空间的初始化函数**
        cache->thread_state = 0;
        cache->owner_tid = (uint32_t)syscall(SYS_gettid);
    }
    return &s_tls_cache;
}
```

---

## 五、全局数组访问逻辑

### 更新逻辑：

- 仅在 `zora_ensure_thread_registered` 中原子写入一次。
- 线程调用 zc_writer_unregister` 或 `zc_reader_unregister` 时释放指向的内容，然后数组中的对应项清零

### 访问逻辑伪代码（所有数据访问 API 内部调用）：

```c
// 函数签名可能需要修改，实际 tid 会因为区分写入者和读取者而位数不同
static inline zora_thread_cache_t* zora_get_cache_by_tid(uint32_t tid) {
    if (tid >= MAX_THREAD_COUNT) return NULL;
    return g_thread_cache_pool[tid];
}
```

---

## 六、主要 API 路径

### 路径一：申请块（凭 ID）

调用链示例：`zc_writer_acquire_block(tid, size, out_handle)`

1. 根据 tid 查表访问线程缓存。
2. 根据缓存访问工作空间。
3. 生成句柄：
   ```c
   uint64_t va = (uint64_t)&zc_block_header;
   zc_handle_t out_handle = ((uint64_t)cache->current_version << 56) | (va & 0x00FFFFFFFFFFFFFFULL);
   out_handle ^= cache->encryption_key;
   ```
4. 返回 `out_handle` 给外部线程。

### 路径二：访问块（凭 ID 和 handle）

调用链示例：`zc_store(tid, handle, offset, value, expected_desc)`

1. 调用 `zora_get_cache_by_tid(tid)` 获取缓存块指针。
2. 若指针为 NULL → 失败。
3. 调用 `zora_decrypt_and_validate(handle, cache->encryption_key, cache->current_version, &va)`：
   - 解密句柄，校验版本号。
   - 若失败 → 返回错误。
4. 执行实际写入逻辑。

---

## 七、安全与性能注释
- 防重放攻击：版本号机制确保旧句柄在结束一个块访问后失效。
- 防地址泄露：句柄中无明文地址，密钥不暴露给用户。
- 零共享缓存：TLCB 独占缓存行，避免 false sharing。
- 快速路径优化：TLS + 轻量加密，确保 99% 访问在 5 cycles 内完成。
- 密钥轮换无感：仅在 commit/cancel/release 时更新，不影响中间多次读写。