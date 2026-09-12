#ifndef STRIDE_TYPES_H
#define STRIDE_TYPES_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================
 * Stride 共享类型
 *
 * 唯一被所有能力共同依赖的层，只包含：
 *   1. 操作符（operator）序列的结构 —— 词法分析与后续编译的中间表示（IR）
 *   2. 二进制片段（blob）与状态码
 *
 * 各能力的依赖关系（无环）：
 *
 *                types.h
 *               /   |   \
 *   matcher.h  extractor.h  compiler.h
 *
 * matcher.h 与 extractor.h 只依赖 types.h，互不依赖；compiler.h 在
 * 两者之上提供“模式 → 两个序列”的编排。
 *
 * 单位约定：
 *   - 步长（stride）的单位是**比特**（每一步跨越多少比特）
 *   - 位置与偏移以**步**计（比特偏移 = 步 × 步长）
 *   - 字面量与参数长度以**比特**计（1 字节 = 8 比特，见 STRIDE_BITS）
 *
 * 注意：Stride 与分隔符无关。段（segment）就是一个不透明的二进制，
 * 如何切分输入（例如按 '/' 切分 URL）由调用者负责。
 * ============================================================ */

/* ==================== 二进制片段 ==================== */

/**
 * 二进制片段 —— 任意比特串
 *
 * @c data 指向原始数据，@c bit_len 给出**比特长度**。
 * 本结构不表达所有权：操作符 IR 中的 blob 由 stride_ops_free 释放，
 * 编译产物中的 blob 由各自的 free 函数释放。
 */
typedef struct {
    const void *data;
    size_t bit_len;
} stride_blob_t;

/** 字节数 → 比特数 */
#define STRIDE_BITS(nbytes) ((size_t)(nbytes) * 8u)

/* ==================== 操作符定义 ==================== */

/**
 * 操作符类型 —— 对应语法规范中的 10 种操作符
 */
typedef enum {
    STRIDE_OP_MATCH = 0,     /* $'比特串'   - 精确比对固定比特串 */
    STRIDE_OP_CAPTURE_STEPS, /* ${步数}     - 捕获指定步数 */
    STRIDE_OP_CAPTURE_UNTIL, /* ${'比特串'} - 捕获到指定比特串前 */
    STRIDE_OP_CAPTURE_END,   /* ${}         - 捕获到段尾 */
    STRIDE_OP_JUMP_ABS,      /* $[步位置]   - 绝对定位（基于 HEAD）*/
    STRIDE_OP_JUMP_END,      /* $[END] / $[END-n] - 段尾定位 */
    STRIDE_OP_JUMP_FWD,      /* $[>步数]    - 向段尾方向移动 */
    STRIDE_OP_JUMP_BACK,     /* $[<步数]    - 向段首方向移动 */
    STRIDE_OP_FIND_FWD,      /* $[>'比特串'] - 向段尾方向查找 */
    STRIDE_OP_FIND_REV       /* $[<'比特串'] - 向段首方向查找 */
} stride_op_type_t;

/**
 * 操作符
 *
 * 词法分析的输出，也是匹配序列编译器与提取序列编译器的输入。
 *
 * 注意：携带字面量的操作符（MATCH / CAPTURE_UNTIL / FIND_FWD / FIND_REV）
 * 拥有 data.literal.data 指向的解码后比特串，由 stride_ops_free 释放。
 */
typedef struct {
    stride_op_type_t type;
    union {
        /* MATCH / CAPTURE_UNTIL / FIND_FWD / FIND_REV */
        stride_blob_t literal;

        /* CAPTURE_STEPS / JUMP_ABS / JUMP_FWD / JUMP_BACK */
        size_t steps;

        /* JUMP_END */
        struct {
            int is_end;       /* 是否为 END */
            size_t back_steps; /* END-n 中的 n，0 表示纯 END */
        } jump_end;
    } data;
} stride_op_t;

/**
 * 判断某个操作符是否携带（并拥有）字面量
 */
static inline int stride_op_has_literal(stride_op_type_t type) {
    return type == STRIDE_OP_MATCH || type == STRIDE_OP_CAPTURE_UNTIL ||
           type == STRIDE_OP_FIND_FWD || type == STRIDE_OP_FIND_REV;
}

/* ==================== 状态码 ==================== */

/**
 * 编译状态码 —— 只包含编译器实际会产生的状态
 */
typedef enum {
    STRIDE_OK = 0,            /* 成功 */
    STRIDE_E_INVALID_PATTERN, /* 模式格式无效（词法 / 语法错误）*/
    STRIDE_E_EMPTY_SEGMENT,   /* 空的段模式 */
    STRIDE_E_ALIGN            /* 步长对齐错误：长度不是步长整数倍 */
} stride_status_t;

/**
 * 状态码转可读字符串
 * @return 静态字符串，永不为 NULL
 *
 * 以 static inline 提供，避免为共享层引入额外的编译单元，
 * 使 matcher / extractor / compiler 三部分保持自足。
 */
static inline const char *stride_status_str(stride_status_t status) {
    switch (status) {
        case STRIDE_OK:                return "ok";
        case STRIDE_E_INVALID_PATTERN: return "invalid pattern";
        case STRIDE_E_EMPTY_SEGMENT:   return "empty segment";
        case STRIDE_E_ALIGN:           return "alignment error";
        default:                       return "unknown status";
    }
}

#ifdef __cplusplus
}
#endif

#endif /* STRIDE_TYPES_H */
