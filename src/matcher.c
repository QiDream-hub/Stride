#include "stride/matcher.h"

#include <stdlib.h>
#include <string.h>

#define STRIDE_MATCH_INITIAL_CAPACITY 16

/* ============================================================
 * Stride 匹配序列 - 实现
 *
 * 1. 编译：操作符序列 → 匹配序列（IDLE / HOLD 两状态状态机）
 * 2. 匹配：用匹配序列匹配一个段（以步为单位移动，按比特比对）
 * ============================================================ */

/* ==================== 通用辅助 ==================== */

/* 复制比特串（按整字节复制） */
static int blob_copy(stride_blob_t *dst, const stride_blob_t *src) {
  dst->data = NULL;
  dst->bit_len = src->bit_len;
  if (!src->data || src->bit_len == 0) {
    return 0;
  }
  size_t nbytes = (src->bit_len + 7u) / 8u;
  void *buf = malloc(nbytes);
  if (!buf) {
    return -1;
  }
  memcpy(buf, src->data, nbytes);
  dst->data = buf;
  return 0;
}

static void blob_dispose(stride_blob_t *b) {
  free((void *)b->data);
  b->data = NULL;
  b->bit_len = 0;
}

/* 取第 bit 个比特（MSB 优先） */
static int bit_at(const unsigned char *base, size_t bit) {
  return (base[bit >> 3] >> (7u - (bit & 7u))) & 1;
}

/* 比较 base 从 bit_off 起、长度为 bit_len 的比特串与 data */
static int bits_eq(const unsigned char *base, size_t bit_off, const void *data,
                   size_t bit_len) {
  if (bit_len == 0) {
    return 1;
  }
  if ((bit_off & 7u) == 0 && (bit_len & 7u) == 0) {
    /* 字节对齐快速路径 */
    return memcmp(base + (bit_off >> 3), data, bit_len >> 3) == 0;
  }
  const unsigned char *d = (const unsigned char *)data;
  for (size_t i = 0; i < bit_len; i++) {
    if (bit_at(base, bit_off + i) != bit_at(d, i)) {
      return 0;
    }
  }
  return 1;
}

/* ==================== 编译实现 ==================== */

/**
 * 操作分类（用于状态机）
 */
typedef enum {
  OP_CLASS_STEP_POS,      /* 正向常量步进 */
  OP_CLASS_STEP_NEG,      /* 负向常量步进 */
  OP_CLASS_STEP_ABS_HEAD, /* 绝对步位置（基于 HEAD）*/
  OP_CLASS_STEP_ABS_END,  /* 绝对步位置（基于 END）*/
  OP_CLASS_FIND_FWD,      /* 正向查找 */
  OP_CLASS_FIND_REV,      /* 反向查找 */
  OP_CLASS_LITERAL        /* 比对字面量 */
} op_class_t;

/**
 * 持有元组（状态机内部使用）
 * value 为有符号累计步数：纯偏移按方向加减，END 基准为负累计。
 */
typedef struct {
  int is_find;                /* 是否是查找操作 */
  int is_reverse;             /* 是否反向查找 */
  int is_end_based;           /* 是否基于 END */
  int is_head_abs;            /* 是否是绝对位置（基于 HEAD）*/
  long value;                 /* 有符号累计步数 */
  const stride_blob_t *delim; /* 查找目标（借用操作符的字面量）*/
} hold_tuple_t;

/* 获取操作符的分类和基础元组 */
static int get_op_class(const stride_op_t *op, op_class_t *out_class,
                        hold_tuple_t *out_tuple) {
  memset(out_tuple, 0, sizeof(hold_tuple_t));

  switch (op->type) {
  case STRIDE_OP_CAPTURE_STEPS:
    *out_class = OP_CLASS_STEP_POS;
    out_tuple->value = (long)op->data.steps;
    return 0;

  case STRIDE_OP_CAPTURE_UNTIL:
    *out_class = OP_CLASS_FIND_FWD;
    out_tuple->is_find = 1;
    out_tuple->delim = &op->data.literal;
    return 0;

  case STRIDE_OP_CAPTURE_END:
    *out_class = OP_CLASS_STEP_ABS_END;
    out_tuple->is_end_based = 1;
    out_tuple->value = 0;
    return 0;

  case STRIDE_OP_JUMP_FWD:
    *out_class = OP_CLASS_STEP_POS;
    out_tuple->value = (long)op->data.steps;
    return 0;

  case STRIDE_OP_JUMP_BACK:
    *out_class = OP_CLASS_STEP_NEG;
    out_tuple->value = -(long)op->data.steps;
    return 0;

  case STRIDE_OP_JUMP_ABS:
    *out_class = OP_CLASS_STEP_ABS_HEAD;
    out_tuple->is_head_abs = 1;
    out_tuple->value = (long)op->data.steps;
    return 0;

  case STRIDE_OP_JUMP_END:
    *out_class = OP_CLASS_STEP_ABS_END;
    out_tuple->is_end_based = 1;
    out_tuple->value = -(long)op->data.jump_end.back_steps;
    return 0;

  case STRIDE_OP_FIND_FWD:
    *out_class = OP_CLASS_FIND_FWD;
    out_tuple->is_find = 1;
    out_tuple->delim = &op->data.literal;
    return 0;

  case STRIDE_OP_FIND_REV:
    *out_class = OP_CLASS_FIND_REV;
    out_tuple->is_find = 1;
    out_tuple->is_reverse = 1;
    out_tuple->delim = &op->data.literal;
    return 0;

  case STRIDE_OP_MATCH:
    *out_class = OP_CLASS_LITERAL;
    return 0;

  default:
    return -1;
  }
}

/**
 * 尝试把新的常量事件合并进持有元组
 * @return 0 成功，-1 不可相加
 */
static int try_add_constants(hold_tuple_t *hold, op_class_t new_class,
                             long new_value) {
  if (hold->is_find) {
    return -1;
  }

  if (hold->is_end_based) {
    /* END 基准：只能与负数或 END 相加 */
    if (new_class == OP_CLASS_STEP_POS) {
      return -1; /* END 不能加正数 */
    }
    if (new_class == OP_CLASS_STEP_NEG || new_class == OP_CLASS_STEP_ABS_END) {
      hold->value += new_value;
      return 0;
    }
    return -1;
  }

  /* HEAD 基准或纯偏移 */
  if (new_class == OP_CLASS_STEP_POS || new_class == OP_CLASS_STEP_NEG) {
    hold->value += new_value;
    return 0;
  }
  if (new_class == OP_CLASS_STEP_ABS_HEAD) {
    hold->value = new_value; /* 绝对位置覆盖累计偏移 */
    return 0;
  }
  return -1;
}

/**
 * 输出持有元组到匹配数组
 * steps 统一输出为非负幅度，方向由 type 表达。
 */
static int match_emit(stride_match_op_t **arr, size_t *capacity, size_t *count,
                      const hold_tuple_t *hold, const stride_blob_t *expect) {
  if (*count >= *capacity) {
    size_t new_cap = *capacity * 2;
    stride_match_op_t *na =
        (stride_match_op_t *)realloc(*arr, new_cap * sizeof(stride_match_op_t));
    if (!na) {
      return -1;
    }
    memset(na + *capacity, 0,
           (new_cap - *capacity) * sizeof(stride_match_op_t));
    *arr = na;
    *capacity = new_cap;
  }

  stride_match_op_t *m = &(*arr)[*count];
  memset(m, 0, sizeof(*m));

  if (hold->is_find) {
    m->type = hold->is_reverse ? STRIDE_MT_FIND_REV : STRIDE_MT_FIND_FWD;
    if (blob_copy(&m->delimiter, hold->delim) != 0) {
      return -1;
    }
  } else if (hold->is_head_abs) {
    m->type = STRIDE_MT_ABS_HEAD;
    /* HEAD + 负偏移恒不成立：用 SIZE_MAX 让执行期失败 */
    m->steps = (hold->value < 0) ? (size_t)-1 : (size_t)hold->value;
  } else if (hold->is_end_based) {
    m->type = STRIDE_MT_ABS_END;
    m->steps = (hold->value > 0) ? 0 : (size_t)(-hold->value);
  } else if (hold->value >= 0) {
    m->type = STRIDE_MT_STEP_FWD;
    m->steps = (size_t)hold->value;
  } else {
    m->type = STRIDE_MT_STEP_BACK;
    m->steps = (size_t)(-hold->value);
  }

  if (expect && expect->bit_len > 0) {
    if (blob_copy(&m->expect, expect) != 0) {
      blob_dispose(&m->delimiter);
      return -1;
    }
  }

  (*count)++;
  return 0;
}

int stride_match_compile(const stride_op_t *ops, size_t op_count, size_t stride,
                         stride_match_op_t **out_ops, size_t *out_count,
                         size_t *out_capacity) {
  if (!ops || !out_ops || !out_count || !out_capacity) {
    return -1;
  }

  *out_ops = NULL;
  *out_count = 0;
  *out_capacity = STRIDE_MATCH_INITIAL_CAPACITY;

  stride_match_op_t *arr =
      (stride_match_op_t *)calloc(*out_capacity, sizeof(stride_match_op_t));
  if (!arr) {
    return -1;
  }

  hold_tuple_t hold;
  memset(&hold, 0, sizeof(hold));
  int state = 0; /* 0 = IDLE, 1 = HOLD */
  size_t count = 0;

  for (size_t i = 0; i < op_count; i++) {
    const stride_op_t *op = &ops[i];
    op_class_t op_class;
    hold_tuple_t tuple;

    if (get_op_class(op, &op_class, &tuple) != 0) {
      continue; /* 跳过无效操作 */
    }

    /* 编译期已知步长时，字面量必须步对齐 */
    if (op_class == OP_CLASS_LITERAL && stride != 0 &&
        op->data.literal.bit_len % stride != 0) {
      goto fail;
    }
    if ((op_class == OP_CLASS_FIND_FWD || op_class == OP_CLASS_FIND_REV) &&
        stride != 0 && op->data.literal.bit_len % stride != 0) {
      goto fail;
    }

    if (state == 0) { /* IDLE */
      if (op_class == OP_CLASS_LITERAL) {
        /* IDLE + 字面量：直接输出 (0, literal)，保持 IDLE */
        if (match_emit(&arr, out_capacity, &count, &hold, &op->data.literal) !=
            0) {
          goto fail;
        }
      } else {
        hold = tuple;
        state = 1; /* HOLD */
      }
    } else { /* HOLD */
      if (op_class == OP_CLASS_LITERAL) {
        /* HOLD + 字面量：合并输出 (持有值, literal) */
        if (match_emit(&arr, out_capacity, &count, &hold, &op->data.literal) !=
            0) {
          goto fail;
        }
        memset(&hold, 0, sizeof(hold));
        state = 0; /* IDLE */
      } else if (try_add_constants(&hold, op_class, tuple.value) == 0) {
        /* 常量可相加，保持 HOLD */
      } else {
        /* 不可相加：先输出持有，再持有新元组 */
        if (match_emit(&arr, out_capacity, &count, &hold, NULL) != 0) {
          goto fail;
        }
        hold = tuple;
        state = 1; /* HOLD */
      }
    }
  }

  /* 扫描结束：若在 HOLD，输出持有 */
  if (state == 1) {
    if (match_emit(&arr, out_capacity, &count, &hold, NULL) != 0) {
      goto fail;
    }
  }

  *out_ops = arr;
  *out_count = count;
  return 0;

fail:
  stride_match_free(arr, count + 1);
  *out_ops = NULL;
  *out_count = 0;
  *out_capacity = 0;
  return -1;
}

void stride_match_free(stride_match_op_t *ops, size_t count) {
  if (!ops) {
    return;
  }
  for (size_t i = 0; i < count; i++) {
    free((void *)ops[i].delimiter.data);
    free((void *)ops[i].expect.data);
  }
  free(ops);
}

/* ==================== 匹配实现 ==================== */

int stride_match_run(const stride_match_op_t *ops, size_t count, size_t stride,
                     const void *segment, size_t segment_bit_len) {

  if (!segment || (count > 0 && !ops)) {
    return -1;
  }
  if (stride == 0) {
    stride = 1;
  }
  if (segment_bit_len % stride != 0) {
    return -1; /* 段长不是步长整数倍 */
  }

  const unsigned char *seg = (const unsigned char *)segment;
  size_t total = segment_bit_len / stride; /* 总步数 N */
  size_t pos = 0;                          /* 始终满足 pos <= total */

  for (size_t i = 0; i < count; i++) {
    const stride_match_op_t *m = &ops[i];
    size_t v = m->steps;

    switch (m->type) {
    case STRIDE_MT_STEP_FWD:
      if (v > total - pos) {
        goto fail;
      }
      pos += v;
      break;

    case STRIDE_MT_STEP_BACK:
      if (v > pos) {
        goto fail;
      }
      pos -= v;
      break;

    case STRIDE_MT_ABS_HEAD:
      if (v > total) {
        goto fail;
      }
      pos = v;
      break;

    case STRIDE_MT_ABS_END:
      if (v > total) {
        goto fail;
      }
      pos = total - v;
      break;

    case STRIDE_MT_FIND_FWD: {
      if (m->delimiter.bit_len == 0 || m->delimiter.bit_len % stride != 0) {
        goto fail;
      }
      size_t ds = m->delimiter.bit_len / stride;
      int found = 0;
      for (size_t k = pos; k + ds <= total; k++) {
        if (bits_eq(seg, k * stride, m->delimiter.data, m->delimiter.bit_len)) {
          pos = k;
          found = 1;
          break;
        }
      }
      if (!found) {
        goto fail;
      }
      break;
    }

    case STRIDE_MT_FIND_REV: {
      if (total == 0 || m->delimiter.bit_len == 0 ||
          m->delimiter.bit_len % stride != 0) {
        goto fail;
      }
      size_t ds = m->delimiter.bit_len / stride;
      size_t k = (pos == 0) ? total - 1 : pos - 1;
      int found = 0;
      for (;;) {
        if (k + ds <= total &&
            bits_eq(seg, k * stride, m->delimiter.data, m->delimiter.bit_len)) {
          pos = k;
          found = 1;
          break;
        }
        if (k == 0) {
          break;
        }
        k--;
      }
      if (!found) {
        goto fail;
      }
      break;
    }

    default:
      goto fail;
    }

    /* 字面量在移动后的游标处比对，并消耗其步数 */
    if (m->expect.bit_len > 0) {
      if (m->expect.bit_len % stride != 0) {
        goto fail;
      }
      size_t es = m->expect.bit_len / stride;
      if (es > total - pos) {
        goto fail;
      }
      if (!bits_eq(seg, pos * stride, m->expect.data, m->expect.bit_len)) {
        goto fail;
      }
      pos += es;
    }
    continue;

  fail:
    return -i;
  }

  /* 段尾对齐 */
  if (pos != total) {
    return -count;
  }
  return 0;
}