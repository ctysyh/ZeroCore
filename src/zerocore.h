/*
 * ZeroCore - High-performance lock-free streaming engine core
 * Version: 1.0
 * Author: Bib Guake
 * License: GPLv3
 * Copyright (C) 2025 Bib Guake
 */

#ifndef ZEROCORE_H
#define ZEROCORE_H

#include <stdatomic.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ======================== 配置与平台宏 ======================== */

#ifndef ZC_CACHE_LINE_SIZE
#define ZC_CACHE_LINE_SIZE 512
#endif

#ifndef ZC_MAX_WRITERS
#define ZC_MAX_WRITERS 64
#endif

#ifndef ZC_MAX_READERS
#define ZC_MAX_READERS 64
#endif

#ifndef ZC_MAX_CLEANERS
#define ZC_MAX_CLEANERS 64
#endif

#ifndef ZC_BIG_ENDIAN
// 默认小端，如需大端请在编译时定义 ZC_BIG_ENDIAN
#endif

/* ======================== 基础类型定义 ======================== */

typedef uint64_t zc_writer_id_t;
typedef uint64_t zc_reader_id_t;
typedef uint64_t zc_cleaner_id_t;
typedef uint32_t zc_token_t;

/* 不透明句柄，用户不可直接访问内部 */
typedef struct zc_block_handle zc_block_handle_t;

/* ======================== 枚举定义 ======================== */

typedef enum {
    ZC_OK = 0,
    ZC_ERROR_INVALID_PARAM,
    ZC_ERROR_OUT_OF_MEMORY,
    ZC_ERROR_TIMEOUT,
    ZC_ERROR_NOT_FOUND,
    ZC_ERROR_ALREADY_EXISTS,
    ZC_ERROR_BUSY,
    ZC_ERROR_SHUTDOWN,
    ZC_ERROR_MAX
} zc_result_t;

typedef enum {
    ZC_LOG_ERROR = 0,
    ZC_LOG_WARN,
    ZC_LOG_INFO,
    ZC_LOG_DEBUG,
    ZC_LOG_TRACE
} zc_log_level_t;

typedef enum {
    ZC_STATE_FREE = 0,
    ZC_STATE_USING,
    ZC_STATE_CLEAN
} zc_block_state_t;

typedef enum {
    ZC_MSG_CLEAN_HINT = 1,      // 清理者推荐写入地址
    ZC_MSG_BACKPRESSURE,        // 背压警告
    ZC_MSG_JUMP_ALERT,          // 跳跃频繁警告
    ZC_MSG_MISSING_BLOCK,       // 读取者遗漏块通知
    ZC_MSG_STREAM_IDLE,         // 流空闲警告
    ZC_MSG_CUSTOM_BASE = 1000   // 用户自定义消息起始值
} zc_message_type_t;

typedef enum {
    ZC_HOOK_BEFORE_ALLOC = 0,
    ZC_HOOK_AFTER_COMMIT,
    ZC_HOOK_BEFORE_CLEAN,
    ZC_HOOK_AFTER_MERGE,
    ZC_HOOK_ON_JUMP,
    ZC_HOOK_MAX
} zc_hook_event_t;

/* ======================== 消息结构 ======================== */

typedef struct {
    zc_message_type_t type;
    uint64_t          payload;  // 地址偏移 / 时间戳 / token / 自定义
    uint32_t          sender_id; // 发送者ID（cleaner_id 或 system）
    uint32_t          reserved;
} zc_message_t;

/* ======================== 统计结构 ======================== */

typedef struct {
    uint64_t free_block_count;
    uint64_t using_block_count;
    uint64_t clean_ops;
    uint64_t merge_ops;
    uint64_t backpressure_events;
    uint64_t jump_count;
    uint64_t total_bytes;
    uint64_t used_bytes;
    uint64_t heartbeat_missed;  // 心跳超时次数
    uint64_t reserved[8];
} zc_stats_t;

/* ======================== 配置结构 ======================== */

typedef struct {
    size_t pool_size;           // 内存池总大小（字节），必须 >= 4MB
    size_t heartbeat_interval;  // 心跳间隔（纳秒），默认100ms = 100000000
    size_t msg_queue_size;      // 每线程消息队列长度，默认16
    bool   enable_cleaner;      // 是否启用清理者线程
    size_t cleaner_count;       // 清理者线程数（默认1，最大8）
    size_t cleaner_stride;      // 清理者并行步进偏移（字节）
    char   reserved[64];        // 预留扩展字段，必须清零
} zc_config_t;

/* ======================== 钩子回调 ======================== */

typedef void (*zc_hook_callback_t)(zc_hook_event_t event, void* data, void* user_ctx);

/* ======================== 清理策略接口 ======================== */

typedef struct zc_clean_strategy zc_clean_strategy_t;

struct zc_clean_strategy {
    const char* name;
    void (*init)(void* ctx);
    void (*destroy)(void* ctx);
    uint64_t (*select_clean_start)(void* ctx, void* pool); // pool 为不透明指针
    bool (*should_merge)(void* ctx, void* header_a, void* header_b);
    uint64_t (*suggest_write_address)(void* ctx, void* pool, zc_writer_id_t writer_id);
    void* ctx; // 用户上下文，ZeroCore不管理其生命周期
};

/* ======================== 分配策略接口 ======================== */

typedef struct zc_alloc_strategy zc_alloc_strategy_t;

struct zc_alloc_strategy {
    const char* name;
    void (*init)(void* ctx);
    void (*destroy)(void* ctx);
    uint64_t (*find_free_block)(void* ctx, void* pool, size_t size, zc_writer_id_t writer_id);
    void* ctx;
};

/* ======================== 背压策略接口 ======================== */

typedef struct zc_backpressure_strategy zc_backpressure_strategy_t;

struct zc_backpressure_strategy {
    const char* name;
    void (*init)(void* ctx);
    void (*destroy)(void* ctx);
    bool (*should_throttle)(void* ctx, void* pool, zc_stats_t* stats);
    void (*on_throttle)(void* ctx, zc_writer_id_t writer_id);
    void* ctx;
};

/* ======================== 系统级接口 ======================== */

/**
 * @brief 初始化ZeroCore引擎
 * @param config 配置结构体，必须有效
 * @return ZC_OK 或错误码
 * @note 线程安全（全局互斥）
 */
zc_result_t zc_init(const zc_config_t* config);

/**
 * @brief 销毁ZeroCore引擎
 * @param force 是否强制销毁（不等待优雅关闭）
 * @return ZC_OK 或错误码
 * @note 线程安全
 */
zc_result_t zc_destroy(bool force);

/**
 * @brief 软重启：重置状态机，不清空内存池
 * @return ZC_OK 或错误码
 * @note 线程安全，需在无活跃操作时调用
 */
zc_result_t zc_soft_restart(void);

/**
 * @brief 更新运行时配置（部分字段）
 * @param new_config 新配置（仅部分字段生效）
 * @return ZC_OK 或错误码
 * @note 线程安全，支持热更新
 */
zc_result_t zc_update_config(const zc_config_t* new_config);

/**
 * @brief 获取当前统计快照
 * @param out_stats 输出统计结构
 * @note 线程安全，原子快照
 */
void zc_stats_snapshot(zc_stats_t* out_stats);

/**
 * @brief 调试：输出内存池状态到文件
 * @param fp 输出文件指针
 * @note 仅在DEBUG模式有效，非线程安全（需暂停引擎）
 */
void zc_dump_pool(FILE* fp);

/**
 * @brief 设置日志级别
 * @param level 日志级别
 */
void zc_set_log_level(zc_log_level_t level);

/**
 * @brief 注册钩子回调
 * @param event 事件类型
 * @param cb 回调函数
 * @param user_ctx 用户上下文
 * @return ZC_OK 或错误码
 * @note 线程安全
 */
zc_result_t zc_register_hook(zc_hook_event_t event, zc_hook_callback_t cb, void* user_ctx);

/* ======================== 写入者接口 ======================== */

/**
 * @brief 注册写入者
 * @param token 数据流标识符
 * @param out_writer_id 输出分配的写入者ID
 * @return ZC_OK 或错误码
 * @note 线程安全
 */
zc_result_t zc_writer_register(zc_token_t token, zc_writer_id_t* out_writer_id);

/**
 * @brief 注销写入者（优雅释放引用块）
 * @param writer_id 写入者ID
 * @return ZC_OK 或错误码
 * @note 线程安全
 */
zc_result_t zc_writer_unregister(zc_writer_id_t writer_id);

/**
 * @brief 获取空闲内存块
 * @param writer_id 写入者ID
 * @param size 请求大小（字节）
 * @param timeout_ns 超时时间（纳秒），0=不等待
 * @return 块句柄，失败返回NULL
 * @note 线程安全（每writer_id独立）
 */
zc_block_handle_t* zc_writer_acquire_block(zc_writer_id_t writer_id, size_t size, uint64_t timeout_ns);

/**
 * @brief 提交写入完成
 * @param handle 块句柄
 * @return ZC_OK 或错误码
 * @note 必须在acquire后调用，线程安全
 */
zc_result_t zc_writer_commit_block(zc_block_handle_t* handle);

/**
 * @brief 取消写入（释放块引用）
 * @param handle 块句柄
 * @return ZC_OK 或错误码
 * @note 线程安全
 */
zc_result_t zc_writer_cancel_block(zc_block_handle_t* handle);

/**
 * @brief 检查并拉取消息
 * @param writer_id 写入者ID
 * @param out_msgs 输出消息数组
 * @param max_count 数组最大长度
 * @return 实际拉取消息数
 * @note 线程安全
 */
int zc_writer_check_messages(zc_writer_id_t writer_id, zc_message_t* out_msgs, int max_count);

/**
 * @brief 发送心跳
 * @param writer_id 写入者ID
 * @note 线程安全
 */
void zc_writer_send_heartbeat(zc_writer_id_t writer_id);

/* ======================== 读取者接口 ======================== */

/**
 * @brief 注册读取者
 * @param token_mask 关注的token位掩码（bit N=1 表示关注token N）
 * @param out_reader_id 输出分配的读取者ID
 * @return ZC_OK 或错误码
 * @note 线程安全
 */
zc_result_t zc_reader_register(uint32_t token_mask, zc_reader_id_t* out_reader_id);

/**
 * @brief 注销读取者
 * @param reader_id 读取者ID
 * @return ZC_OK 或错误码
 * @note 线程安全
 */
zc_result_t zc_reader_unregister(zc_reader_id_t reader_id);

/**
 * @brief 获取下一个可读块
 * @param reader_id 读取者ID
 * @param timeout_ns 超时时间（纳秒），0=不等待
 * @return 块句柄，失败返回NULL
 * @note 线程安全（每reader_id独立）
 */
zc_block_handle_t* zc_reader_poll_block(zc_reader_id_t reader_id, uint64_t timeout_ns);

/**
 * @brief 释放读取块引用
 * @param handle 块句柄
 * @return ZC_OK 或错误码
 * @note 必须在poll后调用，线程安全
 */
zc_result_t zc_reader_release_block(zc_block_handle_t* handle);

/**
 * @brief 检查并拉取消息
 * @param reader_id 读取者ID
 * @param out_msgs 输出消息数组
 * @param max_count 数组最大长度
 * @return 实际拉取消息数
 * @note 线程安全
 */
int zc_reader_check_messages(zc_reader_id_t reader_id, zc_message_t* out_msgs, int max_count);

/**
 * @brief 发送心跳
 * @param reader_id 读取者ID
 * @note 线程安全
 */
void zc_reader_send_heartbeat(zc_reader_id_t reader_id);

/* ======================== 清理者接口 ======================== */

/**
 * @brief 启动清理者线程
 * @param cleaner_id 清理者ID（0 ~ cleaner_count-1）
 * @return ZC_OK 或错误码
 * @note 线程安全
 */
zc_result_t zc_cleaner_start(zc_cleaner_id_t cleaner_id);

/**
 * @brief 停止清理者线程（优雅退出）
 * @param cleaner_id 清理者ID
 * @return ZC_OK 或错误码
 * @note 线程安全
 */
zc_result_t zc_cleaner_stop(zc_cleaner_id_t cleaner_id);

/**
 * @brief 设置清理策略（可热替换）
 * @param cleaner_id 清理者ID
 * @param strategy 策略结构体（需保持生命周期）
 * @return ZC_OK 或错误码
 * @note 线程安全
 */
zc_result_t zc_cleaner_set_strategy(zc_cleaner_id_t cleaner_id, zc_clean_strategy_t* strategy);

/* ======================== 块句柄访问器（只读） ======================== */

/**
 * @brief 获取块内数据指针
 * @param handle 块句柄
 * @return 数据起始地址（跳过header）
 */
void* zc_block_data(zc_block_handle_t* handle);

/**
 * @brief 获取块大小（用户可用部分）
 * @param handle 块句柄
 * @return 大小（字节）
 */
size_t zc_block_size(zc_block_handle_t* handle);

/**
 * @brief 获取写入者Token
 * @param handle 块句柄
 * @return token
 */
zc_token_t zc_block_token(zc_block_handle_t* handle);

/**
 * @brief 获取时间戳（纳秒）
 * @param handle 块句柄
 * @return 时间戳
 */
uint64_t zc_block_timestamp(zc_block_handle_t* handle);

#ifdef __cplusplus
}
#endif

#endif /* ZEROCORE_H */