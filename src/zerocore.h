/*
 * ZeroCore - High-performance lock-free streaming engine core
 * Version: 1.2
 * Author: Bib Guake
 * License: GPLv3
 * Copyright (C) 2025 Bib Guake
 */

#ifndef ZEROCORE_H
#define ZEROCORE_H

#define ZC_VERSION_MAJOR 1
#define ZC_VERSION_MINOR 2

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
#define ZC_MAX_WRITERS 32
#endif

#ifndef ZC_MAX_READERS_PER_WRITER
#define ZC_MAX_READERS_PER_WRITER 32
#endif

// 默认关闭安全检查（生产环境），调试时启用
#ifndef ZC_ENABLE_SAFETY_CHECKS
// #define ZC_ENABLE_SAFETY_CHECKS 1
#endif

#ifndef ZC_BIG_ENDIAN
// 默认小端，如需大端请在编译时定义 ZC_BIG_ENDIAN
#endif

#ifndef ZC_API
#define ZC_API
#endif

/* ======================== 基础类型定义 ======================== */

typedef uint32_t zc_writer_id_t;
typedef uint64_t zc_reader_id_t;
typedef uint64_t zc_time_t;
typedef uint32_t zc_short_time_t;

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
} zc_result_t; // 用于区分错误类型

typedef enum {
    ZC_LOG_ERROR = 0,
    ZC_LOG_WARN,
    ZC_LOG_INFO,
    ZC_LOG_DEBUG,
    ZC_LOG_TRACE
} zc_log_level_t;

typedef enum __attribute__((__type__(uint16_t))) {
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
    ZC_HOOK_ON_BLOCK_STALE,     // 块长时间未被消费
    ZC_HOOK_ON_THREAD_TIMEOUT,  // 线程超时被回收
    ZC_HOOK_MAX
} zc_hook_event_t;

/* ======================== 统计结构 ======================== */

typedef struct {
    uint64_t total_bytes;
    uint64_t used_bytes;
    uint64_t free_block_count;
    uint64_t using_block_count;
    uint64_t clean_ops;
    uint64_t merge_ops;
    uint64_t max_offset;          // 当前最大偏移量
    uint64_t backpressure_events;
    uint64_t heartbeat_missed;    // 心跳超时次数
    uint64_t reserved[8];
} zc_stats_t;

/* ======================== 配置结构 ======================== */

typedef struct {
    size_t  pool_size;           // 内存池总大小（字节）
    size_t  heartbeat_interval;  // 心跳间隔（纳秒），默认100ms = 100000000
    size_t  cleaner_max_count;   // 最大清理者线程数，0表示不启用清理者
    size_t  region_count;        // 内存池分区数
    size_t  region_size[8];      // 各区域大小
    char    reserved[64];        // 预留扩展字段，必须清零
} zc_config_t;

typedef struct {
    const char*     name;              // 逻辑线程名称
    uint64_t        expected_rate_bps; // 预期速率（字节/秒），0=未知
    uint64_t        thread_id;         // 系统线程ID
} zc_thread_config_t;

/* ======================== 钩子回调 ======================== */

typedef void (*zc_hook_callback_t)(zc_hook_event_t event, void* data, void* user_ctx);

/* ======================== 策略接口 ======================== */

typedef struct zc_clean_strategy zc_clean_strategy_t;
typedef struct zc_alloc_strategy zc_alloc_strategy_t;
typedef struct zc_backpressure_strategy zc_backpressure_strategy_t;

/**
 * @brief 设置清理策略（热替换）
 * @param strategy 策略（需保持生命周期）
 * @return 是否成功
 * @note 线程安全，所有清理者线程自动继承
 */
ZC_API zc_result_t zc_set_clean_strategy(
    zc_clean_strategy_t* strategy
);

/**
 * @brief 设置分配策略
 * @param strategy 策略
 * @return 是否成功
 */
ZC_API zc_result_t zc_set_alloc_strategy(
    zc_alloc_strategy_t* strategy
);

/**
 * @brief 设置背压策略
 * @param strategy 策略
 * @return 是否成功
 */
ZC_API zc_result_t zc_set_backpressure_strategy(
    zc_backpressure_strategy_t* strategy
);

struct zc_clean_strategy {
    const char* name;
    void (*init)(void* ctx);
    void (*destroy)(void* ctx);
    uint64_t (*select_clean_start)(void* ctx, void* pool);
    bool (*should_merge)(void* ctx, void* header_a, void* header_b);
    uint64_t (*suggest_write_address)(void* ctx, void* pool, zc_writer_id_t writer_id);
    void* ctx;
};

struct zc_alloc_strategy {
    const char* name;
    void (*init)(void* ctx);
    void (*destroy)(void* ctx);
    uint64_t (*find_free_block)(void* ctx, void* pool, size_t size, zc_writer_id_t writer_id);
    void* ctx;
};

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
 */
ZC_API zc_result_t zc_init(
    const zc_config_t* config
);

/**
 * @brief 销毁ZeroCore引擎
 * @param force 是否强制销毁（不等待优雅关闭）
 * @return ZC_OK 或错误码
 */
ZC_API zc_result_t zc_destroy(
    bool force
);

/**
 * @brief 软重启：重置状态机，不清空内存池
 * @return ZC_OK 或错误码
 * @note 在异常或无活跃操作时调用
 */
ZC_API zc_result_t zc_soft_restart(void);

/**
 * @brief 更新运行时配置（部分字段）
 * @param new_config 新配置
 * @return ZC_OK 或错误码
 * @note 线程安全，支持热更新
 */
ZC_API zc_result_t zc_update_config(
    const zc_config_t* new_config
);

/**
 * @brief 获取当前统计快照
 * @param out_stats 输出统计结构
 * @note 线程安全，原子快照
 */
ZC_API void zc_stats_snapshot(
    zc_stats_t* out_stats
);

#ifdef ZC_ENABLE_SAFETY_CHECKS
/**
 * @brief 调试：将内存池内容输出到文件
 * @param fp 输出文件指针
 * @note 仅在DEBUG模式有效，需暂停引擎
 */
ZC_API void zc_dump_pool(
    FILE* fp
);
#endif

/**
 * @brief 设置日志级别
 * @param level 日志级别
 */
ZC_API void zc_set_log_level(
    zc_log_level_t level
);

/**
 * @brief 注册钩子回调
 * @param event 事件类型
 * @param cb 回调函数
 * @param user_ctx 用户上下文
 * @return ZC_OK 或错误码
 * @note 线程安全
 */
ZC_API zc_result_t zc_register_hook(
    zc_hook_event_t event,
    zc_hook_callback_t cb,
    void* user_ctx
);

/**
 * @brief 获取系统时间戳（纳秒，单调递增）
 * @return 时间戳
 */
ZC_API zc_time_t zc_timestamp(void);

/* ======================== 写入者接口 ======================== */

/**
 * @brief 注册写入者
 * @param config 线程配置
 * @param out_writer_id 输出分配的写入者ID
 * @return ZC_OK 或错误码
 * @note 线程安全
 */
ZC_API zc_result_t zc_writer_register(
    const zc_thread_config_t* config,
    zc_writer_id_t* out_writer_id
);

/**
 * @brief 注销写入者
 * @param writer_id 写入者ID
 * @return ZC_OK 或错误码
 * @note 线程安全
 */
ZC_API zc_result_t zc_writer_unregister(
    zc_writer_id_t writer_id
);

/**
 * @brief 获取空闲内存块
 * @param writer_id 写入者ID
 * @param size 请求大小（字节）
 * @param handle 获取到的块句柄（不包含header）
 * @param timeout_ns 超时时间（纳秒），0=不等待
 * @return ZC_OK 或错误码
 * @note 线程安全
 */
ZC_API zc_result_t zc_writer_acquire_block(
    zc_writer_id_t writer_id,
    size_t size,
    zc_block_handle_t* handle,
    uint64_t timeout_ns
);

/**
 * @brief 提交写入完成
 * @param handle 块句柄（不包含header）
 * @return ZC_OK 或错误码
 * @note 必须在acquire后调用，线程安全
 */
ZC_API zc_result_t zc_writer_commit_block(
    zc_block_handle_t* handle
);

/**
 * @brief 取消写入（释放块引用）
 * @param handle 块句柄（不包含header）
 * @return ZC_OK 或错误码
 * @note 线程安全
 */
ZC_API zc_result_t zc_writer_cancel_block(
    zc_block_handle_t* handle
);

/**
 * @brief 发送消息
 * @param writer_id
 * @param type 消息类型
 * @param length 消息长度
 * @param msg 消息内容
 * @return ZC_OK 或错误码
 */
ZC_API zc_result_t zc_writer_send_message(
    zc_writer_id_t writer_id,
    zc_message_type_t type,
    uint16_t length,
    const void* msg
);

/**
 * @brief 检查消息
 * @param writer_id
 * @param type 消息类型
 * @param payload 消息负载指针
 * @param length 负载长度
 * @return ZC_OK 或错误码
 */
ZC_API zc_result_t zc_writer_check_message(
    zc_writer_id_t writer_id,
    zc_message_type_t type,
    uint16_t length,
    const void* payload
);

/**
 * @brief 发送心跳
 * @param writer_id 写入者ID
 */
ZC_API void zc_writer_send_heartbeat(
    zc_writer_id_t writer_id
);

/* ======================== 读取者接口 ======================== */

/**
 * @brief 注册读取者
 * @param subscribing_writer 订阅的写入者
 * @param out_reader_id 输出分配的读取者ID
 * @return ZC_OK 或错误码
 * @note 线程安全
 */
ZC_API zc_result_t zc_reader_register(
    zc_writer_id_t subscribing_writer,
    zc_reader_id_t* out_reader_id
);

/**
 * @brief 注销读取者
 * @param reader_id 读取者ID
 * @return ZC_OK 或错误码
 * @note 线程安全
 */
ZC_API zc_result_t zc_reader_unregister(
    zc_reader_id_t reader_id
);

/**
 * @brief 获取一个可读块
 * @param reader_id 读取者ID
 * @param size 请求大小（字节）
 * @param handle 获取到的块句柄（不包含header）
 * @param timeout_ns 超时时间（纳秒），0=不等待
 * @return ZC_OK 或错误码
 * @note 线程安全
 */
ZC_API zc_result_t zc_reader_poll_block(
    zc_reader_id_t reader_id,
    size_t size,
    zc_block_handle_t* handle,
    uint64_t timeout_ns
);

/**
 * @brief 释放读取块引用
 * @param reader_id 读取者ID
 * @param handle 块句柄
 * @return ZC_OK 或错误码
 * @note 必须在poll后调用，线程安全
 */
ZC_API zc_result_t zc_reader_release_block(
    zc_reader_id_t reader_id,
    zc_block_handle_t* handle
);

/**
 * @brief 发送消息
 * @param writer_id
 * @param type 消息类型
 * @param length 消息长度
 * @param msg 消息内容
 * @return ZC_OK 或错误码
 */
ZC_API zc_result_t zc_reader_send_message(
    zc_writer_id_t writer_id,
    zc_message_type_t type,
    uint16_t length,
    const void* msg
);

/**
 * @brief 检查消息
 * @param writer_id
 * @param type 消息类型
 * @param payload 消息负载指针
 * @param length 负载长度
 * @return ZC_OK 或错误码
 */
ZC_API zc_result_t zc_reader_check_message(
    zc_writer_id_t writer_id,
    zc_message_type_t type,
    uint16_t length,
    const void* payload
);

/**
 * @brief 发送心跳
 * @param reader_id 读取者ID
 * @note 线程安全
 */
ZC_API void zc_reader_send_heartbeat(
    zc_reader_id_t reader_id
);

/* ======================== 块句柄访问器 ======================== */

/**
 * @brief 获取块大小（用户可用部分）
 * @param handle 块句柄（不包含header）
 * @return 大小（字节）
 */
ZC_API size_t zc_block_size(
    zc_block_handle_t* handle
);

/**
 * @brief 获取时间戳（纳秒）
 * @param handle 块句柄（不包含header）
 * @return 时间戳
 */
ZC_API uint64_t zc_block_timestamp(
    zc_block_handle_t* handle
);

#ifdef __cplusplus
}
#endif

#endif /* ZEROCORE_H */