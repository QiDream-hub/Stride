#include <stdio.h>
#include <string.h>

#include "stride/stride.h"

/* ============================================================
 * Stride 使用示例
 *
 * 演示：编译一个段模式 → 打印匹配序列 / 提取序列 → 在段上提取参数。
 *
 * 单位：步长以比特计。文本示例统一用步长 8（= 1 字节/步）。
 *
 * 注意：Stride 只处理单个段的内容。如何把输入切分为段（例如按 '/'
 * 切分 URL 路径）由调用者负责。
 * ============================================================ */

/* 文本示例统一使用的步长：1 字节/步 */
#define STEP_BYTE 8u

static const char *match_type_name(stride_match_type_t t) {
    switch (t) {
        case STRIDE_MT_STEP_FWD:  return "STEP_FWD";
        case STRIDE_MT_STEP_BACK: return "STEP_BACK";
        case STRIDE_MT_ABS_HEAD:  return "ABS_HEAD";
        case STRIDE_MT_ABS_END:   return "ABS_END";
        case STRIDE_MT_FIND_FWD:  return "FIND_FWD";
        case STRIDE_MT_FIND_REV:  return "FIND_REV";
        default:                  return "?";
    }
}

static const char *extract_type_name(stride_extractor_op_type_t t) {
    switch (t) {
        case STRIDE_EX_CAPTURE_STEPS: return "CAPTURE_STEPS";
        case STRIDE_EX_CAPTURE_UNTIL: return "CAPTURE_UNTIL";
        case STRIDE_EX_CAPTURE_END:   return "CAPTURE_END";
        case STRIDE_EX_SKIP_BITS:     return "SKIP_BITS";
        case STRIDE_EX_JUMP_ABS:      return "JUMP_ABS";
        case STRIDE_EX_JUMP_END:      return "JUMP_END";
        case STRIDE_EX_JUMP_FWD:      return "JUMP_FWD";
        case STRIDE_EX_JUMP_BACK:     return "JUMP_BACK";
        case STRIDE_EX_FIND_FWD:      return "FIND_FWD";
        case STRIDE_EX_FIND_REV:      return "FIND_REV";
        default:                      return "?";
    }
}

static void print_compile_result(const stride_compile_result_t *r) {
    printf("匹配序列（%zu 个元组）:\n", r->match_count);
    for (size_t i = 0; i < r->match_count; i++) {
        const stride_match_op_t *m = &r->match[i];
        printf("  [%zu] %-11s steps=%-3zu", i, match_type_name(m->type),
               m->steps);
        if (m->delimiter.bit_len) {
            printf(" find=\"%.*s\"(%zu bits)", (int)(m->delimiter.bit_len / 8),
                   (const char *)m->delimiter.data, m->delimiter.bit_len);
        }
        if (m->expect.bit_len) {
            printf(" expect=\"%.*s\"(%zu bits)", (int)(m->expect.bit_len / 8),
                   (const char *)m->expect.data, m->expect.bit_len);
        }
        printf("\n");
    }

    printf("提取序列（%zu 个操作，%zu 个参数）:\n", r->extract_count,
           r->param_count);
    for (size_t i = 0; i < r->extract_count; i++) {
        const stride_extractor_op_t *e = &r->extract[i];
        printf("  [%zu] %s", i, extract_type_name(e->type));
        switch (e->type) {
            case STRIDE_EX_CAPTURE_STEPS:
                printf(" steps=%zu", e->data.capture_steps.steps);
                break;
            case STRIDE_EX_CAPTURE_UNTIL:
                printf(" until=\"%.*s\"",
                       (int)(e->data.capture_until.bit_len / 8),
                       (const char *)e->data.capture_until.data);
                break;
            case STRIDE_EX_SKIP_BITS:
                printf(" bit_len=%zu", e->data.skip_bits.bit_len);
                break;
            case STRIDE_EX_JUMP_ABS:
                printf(" steps=%zu", e->data.jump_abs.steps);
                break;
            case STRIDE_EX_JUMP_END:
                printf(" END-%zu", e->data.jump_end.back_steps);
                break;
            case STRIDE_EX_JUMP_FWD:
                printf(" steps=%zu", e->data.jump_fwd.steps);
                break;
            case STRIDE_EX_JUMP_BACK:
                printf(" steps=%zu", e->data.jump_back.steps);
                break;
            case STRIDE_EX_FIND_FWD:
                printf(" find=\"%.*s\"", (int)(e->data.find_fwd.bit_len / 8),
                       (const char *)e->data.find_fwd.data);
                break;
            case STRIDE_EX_FIND_REV:
                printf(" find=\"%.*s\"", (int)(e->data.find_rev.bit_len / 8),
                       (const char *)e->data.find_rev.data);
                break;
            default:
                break;
        }
        printf("\n");
    }
}

/* 编译模式，并在给定段上提取参数（步长 8） */
static void demo(const char *pattern, const char *segment) {
    printf("--------------------------------------------------\n");
    printf("模式: %s\n", pattern);
    printf("段:   \"%s\"\n\n", segment);

    stride_compile_result_t r = stride_compile_ex(pattern, 0, STEP_BYTE);
    if (r.status != STRIDE_OK) {
        printf("编译失败: %s (%s)\n\n", stride_status_str(r.status),
               r.error_msg ? r.error_msg : "?");
        stride_compile_free(&r);
        return;
    }

    print_compile_result(&r);

    size_t seg_bits = STRIDE_BITS(strlen(segment));

    /* 匹配：只判断段是否命中，不做提取 */
    int matched = stride_match_run(r.match, r.match_count, STEP_BYTE, segment,
                                   seg_bits);
    printf("\n匹配: %s\n", matched == 0 ? "命中" : "未命中");

    if (matched != 0) {
        stride_compile_free(&r);
        printf("\n");
        return;
    }

    stride_extractor_t *ex = stride_extractor_create(r.extract, r.extract_count);
    printf("运行提取:\n");

    if (ex) {
        stride_param_t params[16];
        size_t count = 0;

        if (stride_extractor_run(ex, STEP_BYTE, segment, seg_bits, params, 16,
                                 &count) == 0) {
            printf("  共 %zu 个参数\n", count);
            for (size_t i = 0; i < count; i++) {
                printf("    [%zu] \"%.*s\" (%zu bits)\n", i,
                       (int)(params[i].bit_len / 8),
                       (const char *)params[i].ptr, params[i].bit_len);
            }
        } else {
            printf("  提取失败（段与模式不匹配？）\n");
        }
        stride_extractor_destroy(ex);
    }

    stride_compile_free(&r);
    printf("\n");
}

/* UTF-16LE 段：验证「步长 = 码元位宽」的用法 */
static void demo_utf16(void) {
    /* "中A" = AD 4E 41 00（4 字节 = 32 比特，步长 16） */
    static const unsigned char seg[] = {0xAD, 0x4E, 0x41, 0x00};
    const char *pattern = "${1}$'A\\x00'";

    printf("--------------------------------------------------\n");
    printf("模式: %s\n", pattern);
    printf("段:   AD 4E 41 00（UTF-16LE \"中A\"，步长 16）\n\n");

    stride_compile_result_t r = stride_compile_ex(pattern, 0, 16);
    if (r.status != STRIDE_OK) {
        printf("编译失败: %s\n\n", stride_status_str(r.status));
        stride_compile_free(&r);
        return;
    }

    stride_match_detail_t d;
    int matched = stride_match_run_ex(r.match, r.match_count, 16, seg,
                                      STRIDE_BITS(sizeof(seg)), &d);
    printf("匹配: %s（游标 %zu 步）\n", matched == 0 ? "命中" : "未命中",
           d.cursor);

    if (matched == 0) {
        stride_extractor_t *ex =
            stride_extractor_create(r.extract, r.extract_count);
        stride_param_t params[4];
        size_t count = 0;
        if (stride_extractor_run(ex, 16, seg, STRIDE_BITS(sizeof(seg)), params,
                                 4, &count) == 0) {
            for (size_t i = 0; i < count; i++) {
                printf("  参数[%zu] = %zu bits =", i, params[i].bit_len);
                const unsigned char *p =
                    (const unsigned char *)params[i].ptr;
                for (size_t j = 0; j < params[i].bit_len / 8; j++) {
                    printf(" %02X", p[j]);
                }
                printf("\n");
            }
        }
        stride_extractor_destroy(ex);
    }

    stride_compile_free(&r);
    printf("\n");
}

int main(void) {
    printf("Stride %s —— 步进式比特串匹配编译器示例\n\n",
           STRIDE_VERSION_STRING);

    /* 版本号段：$'v'${'.'}$'.'${}  → 提取 ["2", "0"] */
    demo("$'v'${'.'}$'.'${}", "v2.0");

    /* 日期段：${4}$'-'${2}$'-'${2}  → 提取 ["2024", "03", "15"] */
    demo("${4}$'-'${2}$'-'${2}", "2024-03-15");

    /* 文件名段：${'.'}$'.'${}  → 提取 ["document", "pdf"] */
    demo("${'.'}$'.'${}", "document.pdf");

    /* 反向查找：$[<'=']${}  → 提取 ["=alice"] */
    demo("$[<'=']${}", "name=alice");

    /* 回溯捕获：${}$[0]${'.'}$'.'${}  → 提取 ["document.pdf","document","pdf"] */
    demo("${}$[0]${'.'}$'.'${}", "document.pdf");

    /* 多比特定界（UTF-8）：$'用户：'${}  → 提取 ["alice"] */
    demo("$'用户：'${}", "用户：alice");

    /* 匹配失败示例：段尾未对齐 */
    demo("${4}$'-'${2}$'-'${2}", "20240315");

    /* 编译错误示例 */
    demo("$'unclosed", "whatever");

    /* 定宽编码（UTF-16LE，步长 16） */
    demo_utf16();

    return 0;
}
