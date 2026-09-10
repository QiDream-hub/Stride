#ifndef STRIDE_FEATURE_H
#define STRIDE_FEATURE_H

#include "stride/core.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================
 * Stride 特征序列模块（Feature Sequence）
 *
 * 将操作符序列编译为特征序列。
 * 特征序列用于匹配阶段：每个元组描述一个“移动操作”和在该位置
 * 需要验证的“关键字”。编译采用 IDLE / HOLD 两状态状态机。
 *
 * 见《Stride 编译器设计文档》4.1 节、《Stride 特征序列设计文档》。
 * ============================================================ */

/**
 * 特征元组类型（6 种）
 */
typedef enum {
    STRIDE_FT_CONST_REL_FWD = 0, /* 常量相对向结尾移动：(n, kw) */
    STRIDE_FT_CONST_REL_BACK,    /* 常量相对向开头移动：(-n, kw) */
    STRIDE_FT_CONST_ABS_HEAD,    /* 常量绝对位置（基于 HEAD）：(HEAD+n, kw) */
    STRIDE_FT_CONST_ABS_END,     /* 常量绝对位置（基于 END）：(END-n, kw) */
    STRIDE_FT_DYNAMIC_FIND_FWD,  /* 动态向结尾查找：('c', kw) */
    STRIDE_FT_DYNAMIC_FIND_REV   /* 动态向开头查找：('<', 'c', kw) */
} stride_feature_type_t;

/**
 * 特征元组 —— (移动操作, 关键字) 对
 *
 * keyword 由本模块拥有（stride_feature_compile 会复制一份，
 * 以 '\0' 结尾），通过 stride_feature_free 释放。
 */
typedef struct {
    stride_feature_type_t type;
    int value;              /* 偏移量或字符 ASCII 值 */
    const char *keyword;    /* 关键字，可为 NULL */
    size_t keyword_len;
} stride_feature_t;

/**
 * 特征序列编译：操作符序列 → 特征序列
 * @param ops           操作符数组
 * @param op_count      操作符数量
 * @param out_features  输出特征数组（调用者通过 stride_feature_free 释放）
 * @param out_count     输出特征数量
 * @param out_capacity  输出数组容量
 * @return 0 成功，-1 失败
 *
 * 状态机规则：
 * - IDLE + 常量操作 → HOLD
 * - IDLE + 动态操作 → HOLD
 * - IDLE + 关键字   → 输出 (0, kw)，保持 IDLE
 * - HOLD + 常量操作 → 尝试相加，否则输出持有
 * - HOLD + 动态操作 → 输出持有，持有新元组
 * - HOLD + 关键字   → 输出 (持有值, kw)，清空持有
 */
int stride_feature_compile(const stride_op_t *ops, size_t op_count,
                           stride_feature_t **out_features,
                           size_t *out_count, size_t *out_capacity);

/**
 * 释放特征数组（含每个元组拥有的关键字）
 * @param features 特征数组，可为 NULL
 * @param count    特征数量
 */
void stride_feature_free(stride_feature_t *features, size_t count);

#ifdef __cplusplus
}
#endif

#endif /* STRIDE_FEATURE_H */
