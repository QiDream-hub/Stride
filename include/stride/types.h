#ifndef STRIDE_TYPES_H
#define STRIDE_TYPES_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================
 * Stride 共享类型
 *
 * Stride 的原语：
 *   1. 字节串 blob —— 比对 / 查找的目标是任意长度的二进制
 *
 * 本文件只放类型，不含任何 API：
 *   - sequence.h   序列构建（函数式，尾部合并）与通用执行引擎
 *   - matcher.h    匹配便捷层
 *   - extractor.h  提取便捷层（多段）
 *
 * 序列存储为**单链表** stride_step_t，每个节点表达「先偏移、再执行动作」。
 * 匹配序列与提取序列共用这一结构，因此共用同一套执行引擎。
 *
 * 单位约定：
 *   - 所有位置、偏移、长度：字节
 * ============================================================ */

/* ==================== 字节串 ==================== */

/**
 * 字节串 —— 任意长度的二进制
 *
 * @c data 指向原始数据，@c len 为**字节长度**。
 * 本结构不表达所有权：序列中的 blob 由 stride_seq_free 释放。
 */
typedef struct {
    const void *data;
    size_t len;
} stride_blob_t;

/**
 * 参数 —— 零拷贝的（指针，字节长度）对，直接指向输入段内部
 */
typedef struct {
    const void *ptr;
    size_t len;
} stride_param_t;

/**
 * 由字符串字面量构造字节串（长度按 sizeof 计算，不含结尾 '\0'）
 * 仅适用于字面量，例如：stride_seq_compare(seq, &STRIDE_BLOB_CSTR("ddd:"));
 */
#define STRIDE_BLOB_CSTR(literal) \
    ((stride_blob_t){ (literal), sizeof(literal) - 1u })

/* ==================== 偏移（怎么走） ==================== */

/**
 * 偏移类型 —— 序列节点中的"移动"部分
 *
 * move_value 的单位：**字节**
 */
typedef enum {
    STRIDE_MOVE_NONE = 0,
    STRIDE_MOVE_STEP_FWD,  /* 向段尾走 move_value 字节 */
    STRIDE_MOVE_STEP_BACK, /* 向段首走 move_value 字节 */
    STRIDE_MOVE_ABS_HEAD,  /* 定位到第 move_value 字节（HEAD + n） */
    STRIDE_MOVE_ABS_END,   /* 定位到 END − move_value 字节（0 = 段尾） */
    STRIDE_MOVE_FIND_FWD,  /* 向段尾查找 move_target，落点即其首字节 */
    STRIDE_MOVE_FIND_REV   /* 向段首查找 move_target，落点即其首字节 */
} stride_move_t;

/* ==================== 动作（到位后做什么） ==================== */

typedef enum {
    STRIDE_ACT_NONE = 0,
    STRIDE_ACT_COMPARE,       /* 匹配：在游标处比对 act_target 并前进其字节数 */
    STRIDE_ACT_CAPTURE_BYTES, /* 提取：捕获 act_value 字节 */
    STRIDE_ACT_CAPTURE_UNTIL, /* 提取：捕获到 act_target 前（未找到则到段尾） */
    STRIDE_ACT_CAPTURE_END    /* 提取：捕获到段尾 */
} stride_act_t;

/* ==================== 序列节点 ==================== */

/**
 * 步进节点 —— 「偏移 + 动作」的链表节点
 *
 * move_target / act_target 由本节点拥有（stride_seq_free 释放）。
 */
typedef struct stride_step {
    /* 偏移 */
    stride_move_t move;
    size_t move_value;
    stride_blob_t move_target;

    /* 动作 */
    stride_act_t act;
    stride_blob_t act_target;
    size_t act_value;

    struct stride_step *next;
} stride_step_t;

/**
 * 步进序列 —— 单链表
 *
 * 构建时始终在**尾节点**上尝试合并；执行时从 head 顺序走。
 */
typedef struct {
    stride_step_t *head;
    stride_step_t *tail;
    size_t count;       /* 节点数 */
    size_t param_count; /* 捕获动作个数（提取序列用） */
} stride_seq_t;

/* ==================== 状态码 ==================== */

typedef enum {
    STRIDE_OK = 0,
    STRIDE_E_INVALID_PATTERN, /* 模式格式无效（词法 / 语法错误）*/
    STRIDE_E_EMPTY_SEGMENT,   /* 空的段模式 */
    STRIDE_E_NOMEM            /* 内存分配失败 */
} stride_status_t;

static inline const char *stride_status_str(stride_status_t status) {
    switch (status) {
        case STRIDE_OK:                return "ok";
        case STRIDE_E_INVALID_PATTERN: return "invalid pattern";
        case STRIDE_E_EMPTY_SEGMENT:   return "empty segment";
        case STRIDE_E_NOMEM:           return "out of memory";
        default:                       return "unknown status";
    }
}

#ifdef __cplusplus
}
#endif

#endif /* STRIDE_TYPES_H */
