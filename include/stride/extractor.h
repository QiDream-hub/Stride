#ifndef STRIDE_EXTRACTOR_H
#define STRIDE_EXTRACTOR_H

#include "stride/types.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================
 * Stride 提取序列（Extract Sequence）
 *
 * 只依赖 types.h，不涉及匹配序列。提供两件事：
 *   1. 编译：操作符序列 → 提取序列（含编译时优化）
 *   2. 执行：在段上运行提取序列，产出零拷贝参数
 *
 * 编译时优化：
 *   - 比对操作 STRIDE_OP_MATCH 转换为比特跳过（STRIDE_EX_SKIP_BITS）
 *   - 连续的常量移动合并为一个操作
 *
 * 单位：步长以比特计；steps 为步数；bit_len 为比特长度。
 * ============================================================ */

/**
 * 参数 —— 零拷贝的（指针，比特长度）对，直接指向输入段内部
 *
 * 约定：参数的起始位置必须字节对齐（比特偏移是 8 的整数倍），
 * 因此 ptr 指向包含该比特串的第一个字节。非字节对齐的捕获会失败。
 */
typedef struct {
    const void *ptr;
    size_t      bit_len; /* 比特长度 */
} stride_param_t;

/**
 * 提取器操作类型（10 种）
 */
typedef enum {
    /* 捕获操作（产生参数）*/
    STRIDE_EX_CAPTURE_STEPS = 0, /* 定步捕获 */
    STRIDE_EX_CAPTURE_UNTIL,     /* 捕获到定界串 */
    STRIDE_EX_CAPTURE_END,       /* 捕获到段尾 */

    /* 移动操作（不产生参数）*/
    STRIDE_EX_SKIP_BITS,  /* 跳过字面量比特长度（由 STRIDE_OP_MATCH 优化而来）*/
    STRIDE_EX_JUMP_ABS,   /* 绝对定位（基于 HEAD）*/
    STRIDE_EX_JUMP_END,   /* END 定位 */
    STRIDE_EX_JUMP_FWD,   /* 向段尾移动 */
    STRIDE_EX_JUMP_BACK,  /* 向段首移动 */
    STRIDE_EX_FIND_FWD,   /* 向段尾查找 */
    STRIDE_EX_FIND_REV    /* 向段首查找 */
} stride_extractor_op_type_t;

/**
 * 提取器操作
 *
 * 产物**与步长无关**：所有操作只保存步数（步）或比特数（比特），
 * 不含任何由步长换算出来的值。执行期由 stride_extractor_run 的步长参数
 * 把 SKIP_BITS.bit_len 折算为步数。因此同一份产物可在不同步长下执行。
 */
typedef struct {
    stride_extractor_op_type_t type;
    union {
        struct { size_t steps; }     capture_steps;
        stride_blob_t                capture_until; /* 拥有 */
        struct { size_t bit_len; }   skip_bits;
        struct { size_t steps; }     jump_abs;
        struct { int is_end; size_t back_steps; } jump_end;
        struct { size_t steps; }     jump_fwd;
        struct { size_t steps; }     jump_back;
        stride_blob_t                find_fwd; /* 拥有 */
        stride_blob_t                find_rev; /* 拥有 */
    } data;
} stride_extractor_op_t;

/**
 * 单段提取器 —— 保存一个段完整的（已优化）提取操作序列
 */
typedef struct {
    stride_extractor_op_t *ops;
    size_t                 op_count;
    size_t                 param_count; /* 该提取器产生的参数数量 */
} stride_extractor_t;

/* ==================== 编译 API ==================== */

/**
 * 提取序列编译：操作符序列 → 提取序列（含优化）
 * @param ops             操作符数组
 * @param op_count        操作符数量
 * @param stride          编译期步长（比特/步）；仅用于校验字面量对齐，
 *                        0 表示未知（不校验）。产物本身与步长无关
 * @param out_ops         输出提取操作数组（调用者通过 stride_extractor_free 释放）
 * @param out_count       输出提取操作数量
 * @param out_param_count 输出参数数量
 * @return 0 成功，-1 失败
 */
int stride_extractor_compile(const stride_op_t *ops, size_t op_count,
                             size_t stride, stride_extractor_op_t **out_ops,
                             size_t *out_count, size_t *out_param_count);

/**
 * 释放提取操作数组（含 capture_until / find_* 的比特串副本）
 */
void stride_extractor_free(stride_extractor_op_t *ops, size_t count);

/* ==================== 运行时 API（单段） ==================== */

/**
 * 创建单段提取器（深拷贝一份操作序列）
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
 * @param ex               单段提取器
 * @param stride           执行期步长（比特/步，必须 ≥ 1；0 视为 1）
 * @param segment          段数据（不透明二进制）
 * @param segment_bit_len  段比特长度
 * @param params           参数数组
 * @param param_capacity   参数数组容量
 * @param param_count      输入：已写入的参数数；输出：写入后的参数总数
 * @return 0 成功，-1 失败
 */
int stride_extractor_run(const stride_extractor_t *ex, size_t stride,
                         const void *segment, size_t segment_bit_len,
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
    size_t               segment_count;
    size_t               total_params;
} stride_full_extractor_t;

/**
 * 创建完整提取器
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
 * @param stride         执行期步长（比特/步，所有段共用）
 * @param segments       段数组
 * @param seg_bit_lens   每段比特长度数组（不可为 NULL）
 * @param segment_count  段数
 * @param params         参数数组
 * @param param_capacity 参数数组容量
 * @param out_count      输出参数总数
 * @return 0 成功，-1 失败
 */
int stride_full_extractor_run(const stride_full_extractor_t *full, size_t stride,
                              const void *const *segments,
                              const size_t *seg_bit_lens, size_t segment_count,
                              stride_param_t *params, size_t param_capacity,
                              size_t *out_count);

#ifdef __cplusplus
}
#endif

#endif /* STRIDE_EXTRACTOR_H */
