# ZeroCore Internal 需求文档（v1.3.3-preview）  

---

## 一、概述

本文档在 `v1.3.2` 基础上，结合 **v1.3.3 更新事项** 聚焦 **安全机制的实质性增强**，引入：

- **块尾轻量守卫结构（zc_block_tail_t）**：8B guard_signal + 8B done + 8B checksum，极小开销
- **附着式守卫线程**：每个写入者注册时预创建，绑定到其写入块，实现**纯软件、低延迟越界写监测**
- **指数退避 + cpu_relax 热路径轮询**：平衡性能与响应速度
- **写入者阻断机制**：通过篡改 handle 指针 + 高级语言隔离，实现“软熔断”
- **异常报告机制**：标记污染块，引导清理者隔离处理

---

## 二、块尾守卫结构（v1.3.3）

### 2.1 守卫段数据结构

```c
typedef volatile struct {
    uint64_t guard_signal;   // 守卫量：初始值为魔数，被篡改即触发报警
    uint64_t done;     // 停车标志：非0=运行中，0=停止监测
    uint64_t checksum; // 可选CRC（默认0，启用时计算）
} zc_block_tail_t;
```

#### 布局更新（v1.3.3）：

```
[ Header (64B) ] + [ UserData (N bytes) ] + [ Trailer (24B) ] + [ Padding ]
```

---

## 三、守卫线程机制

### 3.1 守卫线程任务结构

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

> 每个写入者注册时，**预创建一个守卫线程**，初始阻塞在 `pthread_cond_wait`。

---

### 3.2 守卫线程工作循环

```c
static void* zc_guard_thread_main(void* arg) {
    zc_guard_task_t* task = (zc_guard_task_t*)arg;

    while (1) {
        // 1. 等待绑定新块
        pthread_mutex_lock(&task->mutex);
        while (!atomic_load(&task->ready)) {
            pthread_cond_wait(&task->go, &task->mutex);
        }
        pthread_mutex_unlock(&task->mutex);

        zc_block_tail_t* b = task->block;
        if (!b) continue;

        // 2. 初始化监测状态
        uint64_t prev = b->canary;
        uint32_t exp = 1; // 指数退避基数
        atomic_store(&task->triggered, false);

        // 3. 热监测循环
        while (likely(b->done != 0)) {
            uint64_t curr = b->canary; // 单次 load

            if (unlikely(curr != prev)) {
                // 越界写入检测到！
                zc_guard_stop_writer(task);   // 阻断写入者
                zc_guard_report(task, curr);  // 报告异常
                break;
            }

            // 指数退避
            for (uint32_t i = 0; i < exp; ++i) {
                cpu_relax(); // PAUSE 指令，降低功耗
            }
            exp = MIN(exp * 2, 256); // 最大退避 256 次
        }

        // 4. 重置状态，等待下一次绑定
        atomic_store(&task->ready, false);
    }

    return NULL;
}
```

> **性能保障**：
> - 热路径仅一次 `load` + 一次比较
> - 无锁、无系统调用
> - `cpu_relax()` 降低 CPU 流水线惩罚
> - 指数退避避免过度轮询

---

## 四、阻断与报告机制（关键设计）

### 4.1 `zc_guard_stop_writer()` —— 软熔断实现

#### 设计思路：
- 在 `zc_writer_acquire_block()` 返回 handle 给用户前，缓存该 handle 指针
- 当守卫触发时，**将 handle 指向的值篡改为 NULL 或特殊告警地址**
- 用户后续通过该 handle 写入时，将触发 **段错误（SIGSEGV）或自定义指针异常**

#### 伪代码：

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

> **可行性分析**：
> - **有效**：用户线程后续通过被篡改的指针访问内存，必然失败。
> - **安全**：篡改的是 ZeroCore 内部缓存的 handle 地址内容，不破坏用户栈。
> - **作弊防御**：若用户线程缓存了指针副本，**则无法防御** → 此时与防御外部线程 use-alfter-free 相同，通过关闭 C 接口，仅提供高级语言封装（如 Rust/Go wrapper），在 wrapper 层做指针校验或使用智能指针。

---

### 4.2 `zc_guard_report()` —— 污染标记与清理者协同

#### 实现思路：
- 为内存块增加一个新状态**CORRUPT**与原有的三个状态（FREE, USING, CLEAN）并列
- 守卫触发报告时，将内存块的状态修改为CORRUPT，然后访问其依附的写入者的工作空间，向清理者发送消息报告完整的异常信息，同时触发 Hook
- 此外，守卫检查其后邻接块块头，通过检验其数据是否在预期内判断其是否有确认的损坏，如果有还需要进行修复（对于空闲块）或者警告（对于正在被使用的块）
- 清理者一方面在遍历时遇到状态为CORRUPT的块直接执行清理，另一方面接收守卫发送的消息为对应的写入者打上“不可信”标记并向外部监控应用报告，用户可以在外部监控中查看到这个信息，并决定允许这个写入者继续正常运行（比如联合调试外部进程时或者外部进程确认可信）还是将其剔除系统。

---

## 五、守卫线程生命周期管理

### 5.1 创建与绑定

```c
// 初始化时为每个写入者预创建守卫线程
zc_result_t zc_guard_init_for_writer(zc_writer_id_t wid) {
    zc_guard_task_t* task = malloc(sizeof(zc_guard_task_t));
    if (!task) return ZC_ERROR_NOMEM;

    task->block = NULL;
    atomic_init(&task->ready, false);
    atomic_init(&task->triggered, false);
    task->user_handle = NULL;
    task->writer_id = wid;

    pthread_mutex_init(&task->mutex, NULL);
    pthread_cond_init(&task->go, NULL);

    // 创建线程
    pthread_t tid;
    int ret = pthread_create(&tid, NULL, zc_guard_thread_main, task);
    if (ret != 0) {
        free(task);
        return ZC_ERROR_THREAD;
    }

    // 分离线程（无需 join）
    pthread_detach(tid);

    // 注册到全局表
    register_guard_task(wid, task);

    return ZC_OK;
}

// 写入者获取块时绑定
static inline void zc_guard_on_attach(zc_guard_task_t* t, zc_block_tail_t* b) {
    if (!t || !b) return;

    pthread_mutex_lock(&t->mutex);
    t->block = b;
    atomic_store(&t->ready, true);
    pthread_cond_signal(&t->go);
    pthread_mutex_unlock(&t->mutex);
}
```

---

### 5.2 释放与复用

- **无需显式释放**：守卫线程是**长期运行、预创建**的。
- 当写入者释放块时，只需设置 `tail->done = 0`，守卫线程自动进入休眠，等待下次绑定。
- **资源开销**：每个写入者一个线程，但大部分时间在 `cpu_relax` 或 `cond_wait`，CPU 占用极低。

---

## 六、测试与验证（v1.3.3 新增）

### 6.1 新增测试场景

| 编号 | 场景 | 目标 |
|------|------|------|
| TC-GUARD-3 | 故意越界写入触发守卫 | 验证阻断与报告 |
| TC-GUARD-4 | 多写入者并发触发守卫 | 验证线程安全 |
| TC-GUARD-5 | 守卫线程唤醒/休眠压力 | 验证生命周期管理 |
| TC-GUARD-6 | 高级语言 wrapper 指针校验 | 验证作弊防御 |

---

## 七、总结

### 主要变更（v1.3.3）

| 模块 | 变更内容 |
|------|---------|
| **安全机制** | 引入 `zc_block_tail_t` + 守卫线程，实现纯软件越界监测 |
| **阻断方案** | 篡改用户 handle 指针 + 高级语言封装防御作弊 |
| **异常处理** | 标记 `CORRUPT`，引导清理者清理污染块 |

---