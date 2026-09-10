#ifndef STRIDE_FEATURE_H
#define STRIDE_FEATURE_H

#include "stride/types.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================
 * Stride 特征序列（Feature Sequence）
 *
 * 特征序列用于匹配阶段：每个元组描述一个“移动操作”和在该位置需要
 * 验证的“关键字”。只依赖 types.h，不涉及提取序列。
 *
 * 本头文件提供两件事：
 *   1. 编译：操作符序列 → 特征序列（IDLE / HOLD 两状态状态机）
 *   2. 匹配：用特征序列匹配一个段（stride_feature_match）
 * ============================================================ */

/* ==================== 数据结构 ==================== */

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
 * value 语义（统一为非负幅度）：
 * | type                 | 含义                                  |
 * |----------------------|---------------------------------------|
 * | CONST_REL_FWD        | 向段尾方向移动的距离 value            |
 * | CONST_REL_BACK       | 向段首方向移动的距离 value            |
 * | CONST_ABS_HEAD       | 目标位置 = HEAD + value               |
 * | CONST_ABS_END        | 目标位置 = END - value（0 表示段尾）  |
 * | DYNAMIC_FIND_FWD     | 要查找字符的 ASCII 值                 |
 * | DYNAMIC_FIND_REV     | 要查找字符的 ASCII 值                 |
 *
 * keyword 由本模块拥有（stride_feature_compile 会复制一份并以 '\0'
 * 结尾），通过 stride_feature_free 释放。
 */
typedef struct {
    stride_feature_type_t type;
    int value;
    const char *keyword; /* 关键字，可为 NULL */
    size_t keyword_len;
} stride_feature_t;

/* ==================== 编译 ==================== */

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

/* ==================== 匹配 ==================== */

/**
 * 匹配详情（用于诊断 / 自建索引）
 *
 * fail_index 的取值：
 * - < count：第 fail_index 个特征元组失败（移动越界、关键字不符或未找到字符）
 * - == count：所有元组都执行成功，但游标未落在段尾（段尾未对齐）
 */
typedef struct {
    int matched;         /* 1 匹配成功，0 失败 */
    size_t fail_index;   /* 失败所在元组下标，或 count */
    size_t cursor;       /* 失败时的游标位置 */
} stride_match_detail_t;

/**
 * 用特征序列匹配一个段
 * @param features     特征数组
 * @param count        特征数量
 * @param segment      段内容
 * @param segment_len  段长度
 * @return 0 匹配成功，-1 不匹配
 *
 * 匹配规则：按顺序执行每个元组的移动；若元组带关键字，则在移动后的
 * 游标处验证关键字并前进其长度；全部元组执行完毕后，游标必须正好等于
 * 段长度（段尾对齐），否则视为不匹配。
 */
int stride_feature_match(const stride_feature_t *features, size_t count,
                         const char *segment, size_t segment_len);

/**
 * 带诊断信息的匹配
 * @param out 可为 NULL；成功与失败都会被填充
 * @return 0 匹配成功，-1 不匹配
 */
int stride_feature_match_ex(const stride_feature_t *features, size_t count,
                            const char *segment, size_t segment_len,
                            stride_match_detail_t *out);

#ifdef __cplusplus
}
#endif

#endif /* STRIDE_FEATURE_H */
