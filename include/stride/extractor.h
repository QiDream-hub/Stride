#ifndef STRIDE_EXTRACTOR_H
#define STRIDE_EXTRACTOR_H

#include "stride/types.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================
 * Stride 提取序列（Extraction Sequence）
 *
 * 只依赖 types.h，不涉及特征序列。提供两件事：
 *   1. 编译：操作符序列 → 提取序列（含编译时优化）
 *   2. 执行：在段上运行提取序列，产出零拷贝参数
 *
 * 编译时优化：
 *   - 匹配操作 STRIDE_OP_MATCH 转换为常量偏移跳过（STRIDE_EX_SKIP_LEN）
 *   - 连续常量移动操作合并为一个
 * ============================================================ */

/**
 * 参数 —— 零拷贝的（指针，长度）对，直接指向输入段内部
 */
typedef struct {
    const char *ptr;
    size_t len;
} stride_param_t;

/**
 * 提取器操作类型（10 种）
 */
typedef enum {
    /* 捕获操作（产生参数）*/
    STRIDE_EX_CAPTURE_LEN = 0, /* 定长捕获 */
    STRIDE_EX_CAPTURE_CHR,     /* 捕获到字符 */
    STRIDE_EX_CAPTURE_END,     /* 捕获到结尾 */

    /* 移动操作（不产生参数）*/
    STRIDE_EX_SKIP_LEN,        /* 跳过固定长度（由 STRIDE_OP_MATCH 优化而来）*/
    STRIDE_EX_JUMP_ABS,        /* 绝对跳转（基于 HEAD）*/
    STRIDE_EX_JUMP_END,        /* END 跳转 */
    STRIDE_EX_JUMP_FWD,        /* 正向移动 */
    STRIDE_EX_JUMP_BACK,       /* 负向移动 */
    STRIDE_EX_FIND_FWD,        /* 正向查找 */
    STRIDE_EX_FIND_REV         /* 反向查找 */
} stride_extractor_op_type_t;

/**
 * 提取器操作
 */
typedef struct {
    stride_extractor_op_type_t type;
    union {
        struct { size_t length; } capture_len;       /* STRIDE_EX_CAPTURE_LEN */
        struct { char ch; } capture_chr;             /* STRIDE_EX_CAPTURE_CHR */
        struct { size_t length; } skip_len;          /* STRIDE_EX_SKIP_LEN */
        struct { size_t pos; } jump_abs;             /* STRIDE_EX_JUMP_ABS */
        struct { int is_end; int offset; } jump_end; /* STRIDE_EX_JUMP_END */
        struct { size_t offset; } jump_fwd;          /* STRIDE_EX_JUMP_FWD */
        struct { size_t offset; } jump_back;         /* STRIDE_EX_JUMP_BACK */
        struct { char ch; } find_fwd;                /* STRIDE_EX_FIND_FWD */
        struct { char ch; } find_rev;                /* STRIDE_EX_FIND_REV */
    } data;
} stride_extractor_op_t;

/**
 * 单段提取器 —— 保存一个段完整的（已优化）提取操作序列
 */
typedef struct {
    stride_extractor_op_t *ops;
    size_t op_count;
    size_t param_count; /* 该提取器产生的参数数量 */
} stride_extractor_t;

/* ==================== 编译 API ==================== */

/**
 * 提取序列编译：操作符序列 → 提取序列（含优化）
 * @param ops             操作符数组
 * @param op_count        操作符数量
 * @param out_ops         输出提取操作数组（调用者 free）
 * @param out_count       输出提取操作数量
 * @param out_param_count 输出参数数量
 * @return 0 成功，-1 失败
 */
int stride_extractor_compile(const stride_op_t *ops, size_t op_count,
                             stride_extractor_op_t **out_ops,
                             size_t *out_count, size_t *out_param_count);

/* ==================== 运行时 API（单段） ==================== */

/**
 * 创建单段提取器（复制一份操作序列）
 * @return 提取器指针，失败返回 NULL
 */
stride_extractor_t *stride_extractor_create(const stride_extractor_op_t *ops,
                                            size_t op_count);

/**
 * 释放单段提取器
 */
void stride_extractor_destroy(stride_extractor_t *ex);

/**
 * 在单个段上执行提取
 * @param ex              单段提取器
 * @param segment         段内容
 * @param segment_len     段长度
 * @param params          参数数组
 * @param param_capacity  参数数组容量
 * @param param_count     输入：已写入的参数数；输出：写入后的参数总数
 * @return 0 成功，-1 失败
 */
int stride_extractor_execute(const stride_extractor_t *ex,
                             const char *segment, size_t segment_len,
                             stride_param_t *params, size_t param_capacity,
                             size_t *param_count);

/* ==================== 运行时 API（多段） ==================== */

/**
 * 完整提取器 —— 按段顺序组合多个单段提取器
 *
 * 注意：stride_full_extractor_create 仅复制指针数组，不接管
 * seg_extractors 数组本身的所有权；但 stride_full_extractor_destroy
 * 会销毁其中每个单段提取器。
 */
typedef struct {
    stride_extractor_t **segments;
    size_t segment_count;
    size_t total_params;
} stride_full_extractor_t;

/**
 * 创建完整提取器
 * @param seg_extractors 每段的提取器数组
 * @param segment_count  段数
 */
stride_full_extractor_t *stride_full_extractor_create(
    stride_extractor_t **seg_extractors, size_t segment_count);

/**
 * 释放完整提取器（含其持有的每个单段提取器）
 */
void stride_full_extractor_destroy(stride_full_extractor_t *full);

/**
 * 执行完整提取（多段，参数按段顺序连接）
 * @param full           完整提取器
 * @param segments       段数组
 * @param seg_lens       每段长度数组（可为 NULL，此时用 strlen）
 * @param segment_count  段数
 * @param params         参数数组
 * @param param_capacity 参数数组容量
 * @param out_count      输出参数总数
 * @return 0 成功，-1 失败
 */
int stride_full_extractor_execute(const stride_full_extractor_t *full,
                                  const char **segments,
                                  const size_t *seg_lens,
                                  size_t segment_count,
                                  stride_param_t *params,
                                  size_t param_capacity,
                                  size_t *out_count);

#ifdef __cplusplus
}
#endif

#endif /* STRIDE_EXTRACTOR_H */
