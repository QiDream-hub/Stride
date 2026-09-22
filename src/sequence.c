#include "stride/sequence.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

/* ============================================================
 * Stride 步进序列 - 实现
 *
 * 构建：单链表 + 尾节点合并（无状态机）
 * 执行：通用引擎 —— 逐节点「偏移 → 动作」
 * ============================================================ */

/* ==================== 查找辅助（前置声明） ==================== */

static int seg_find_fwd(const unsigned char *seg, size_t total, size_t from,
                        const stride_blob_t *target, size_t *hit);
static int seg_find_rev(const unsigned char *seg, size_t from,
                        const stride_blob_t *target, size_t *hit);

/* ==================== 字节串辅助 ==================== */

static int blob_dup(stride_blob_t *dst, const stride_blob_t *src) {
  dst->data = NULL;
  dst->len = src ? src->len : 0;
  if (!src || !src->data || src->len == 0) {
    return 0;
  }
  void *buf = malloc(src->len);
  if (!buf) {
    return -1;
  }
  memcpy(buf, src->data, src->len);
  dst->data = buf;
  return 0;
}

static void blob_dispose(stride_blob_t *b) {
  free((void *)b->data);
  b->data = NULL;
  b->len = 0;
}

/* ==================== 生命周期 ==================== */

stride_seq_t *stride_seq_new(void) {
  return (stride_seq_t *)calloc(1, sizeof(stride_seq_t));
}

void stride_seq_clear(stride_seq_t *seq) {
  if (!seq) {
    return;
  }
  stride_step_t *n = seq->head;
  while (n) {
    stride_step_t *next = n->next;
    blob_dispose(&n->move_target);
    blob_dispose(&n->act_target);
    free(n);
    n = next;
  }
  memset(seq, 0, sizeof(*seq));
}

void stride_seq_free(stride_seq_t *seq) {
  if (!seq) {
    return;
  }
  stride_seq_clear(seq);
  free(seq);
}

size_t stride_seq_count(const stride_seq_t *seq) {
  return seq ? seq->count : 0;
}

size_t stride_seq_param_count(const stride_seq_t *seq) {
  return seq ? seq->param_count : 0;
}

/* ==================== 构建 ==================== */

static stride_step_t *seq_push(stride_seq_t *seq) {
  stride_step_t *n = (stride_step_t *)calloc(1, sizeof(stride_step_t));
  if (!n) {
    return NULL;
  }
  if (seq->tail) {
    seq->tail->next = n;
  } else {
    seq->head = n;
  }
  seq->tail = n;
  seq->count++;
  return n;
}

/**
 * 尝试把 (kind, value) 合并进节点已有偏移。
 * @return 1 已合并，0 不可合并
 */
static int move_try_merge(stride_step_t *n, stride_move_t kind, size_t value) {
  switch (n->move) {
  case STRIDE_MOVE_STEP_FWD:
    if (kind == STRIDE_MOVE_STEP_FWD) {
      n->move_value += value;
      return 1;
    }
    if (kind == STRIDE_MOVE_STEP_BACK) {
      if (value <= n->move_value) {
        n->move_value -= value;
      } else {
        n->move = STRIDE_MOVE_STEP_BACK;
        n->move_value = value - n->move_value;
      }
      return 1;
    }
    return 0;

  case STRIDE_MOVE_STEP_BACK:
    if (kind == STRIDE_MOVE_STEP_BACK) {
      n->move_value += value;
      return 1;
    }
    if (kind == STRIDE_MOVE_STEP_FWD) {
      if (value >= n->move_value) {
        n->move = STRIDE_MOVE_STEP_FWD;
        n->move_value = value - n->move_value;
      } else {
        n->move_value -= value;
      }
      return 1;
    }
    return 0;

  case STRIDE_MOVE_ABS_HEAD:
    if (kind == STRIDE_MOVE_STEP_FWD) {
      n->move_value += value;
      return 1;
    }
    if (kind == STRIDE_MOVE_STEP_BACK && value <= n->move_value) {
      n->move_value -= value;
      return 1;
    }
    return 0; /* 结果为负：交给运行期按"先定位再后退"判定失败 */

  case STRIDE_MOVE_ABS_END:
    if (kind == STRIDE_MOVE_STEP_BACK) {
      n->move_value += value;
      return 1;
    }
    return 0;

  default: /* NONE / FIND_* */
    return 0;
  }
}

/**
 * 追加偏移：尾节点无动作且可合并时就地合并，否则新建尾节点。
 * target 非空（FIND_*）时从不合并。
 */
static int seq_add_move(stride_seq_t *seq, stride_move_t kind, size_t value,
                        const stride_blob_t *target) {
  if (!seq) {
    return -1;
  }
  stride_step_t *t = seq->tail;
  if (t && t->act == STRIDE_ACT_NONE && !target) {
    if (move_try_merge(t, kind, value)) {
      return 0;
    }
  }

  stride_step_t *n = seq_push(seq);
  if (!n) {
    return -1;
  }
  n->move = kind;
  n->move_value = value;
  if (blob_dup(&n->move_target, target) != 0) {
    return -1;
  }
  return 0;
}

/**
 * 追加动作：尾节点尚无动作时绑定到尾节点，否则新建尾节点。
 */
static int seq_add_act(stride_seq_t *seq, stride_act_t act,
                       const stride_blob_t *literal, size_t value) {
  if (!seq) {
    return -1;
  }
  stride_step_t *n = seq->tail;
  if (!n || n->act != STRIDE_ACT_NONE) {
    n = seq_push(seq);
    if (!n) {
      return -1;
    }
  }
  n->act = act;
  n->act_value = value;
  if (blob_dup(&n->act_target, literal) != 0) {
    return -1;
  }
  if (act != STRIDE_ACT_NONE && act != STRIDE_ACT_COMPARE) {
    seq->param_count++;
  }
  return 0;
}

int stride_seq_step_fwd(stride_seq_t *seq, size_t bytes) {
  return seq_add_move(seq, STRIDE_MOVE_STEP_FWD, bytes, NULL);
}
int stride_seq_step_back(stride_seq_t *seq, size_t bytes) {
  return seq_add_move(seq, STRIDE_MOVE_STEP_BACK, bytes, NULL);
}
int stride_seq_abs_head(stride_seq_t *seq, size_t bytes) {
  return seq_add_move(seq, STRIDE_MOVE_ABS_HEAD, bytes, NULL);
}
int stride_seq_abs_end(stride_seq_t *seq, size_t bytes) {
  return seq_add_move(seq, STRIDE_MOVE_ABS_END, bytes, NULL);
}
int stride_seq_find_fwd(stride_seq_t *seq, const stride_blob_t *target) {
  return seq_add_move(seq, STRIDE_MOVE_FIND_FWD, 0, target);
}
int stride_seq_find_rev(stride_seq_t *seq, const stride_blob_t *target) {
  return seq_add_move(seq, STRIDE_MOVE_FIND_REV, 0, target);
}

int stride_seq_compare(stride_seq_t *seq, const stride_blob_t *literal) {
  return seq_add_act(seq, STRIDE_ACT_COMPARE, literal, 0);
}
int stride_seq_capture_bytes(stride_seq_t *seq, size_t bytes) {
  return seq_add_act(seq, STRIDE_ACT_CAPTURE_BYTES, NULL, bytes);
}
int stride_seq_capture_until(stride_seq_t *seq, const stride_blob_t *target) {
  return seq_add_act(seq, STRIDE_ACT_CAPTURE_UNTIL, target, 0);
}
int stride_seq_capture_end(stride_seq_t *seq) {
  return seq_add_act(seq, STRIDE_ACT_CAPTURE_END, NULL, 0);
}

/* ==================== 通用执行引擎 ==================== */

int stride_seq_run(const stride_seq_t *seq, const void *segment,
                   size_t segment_len, stride_param_t *params,
                   size_t param_capacity, size_t *param_count) {
  if (!seq || !segment) {
    return -1;
  }

  const unsigned char *seg = (const unsigned char *)segment;
  size_t total = segment_len;
  size_t pos = 0;
  size_t out_idx = param_count ? *param_count : 0;

  size_t i = 0;
  for (const stride_step_t *n = seq->head; n; n = n->next, i++) {
    /* ---- 1. 偏移 ---- */
    switch (n->move) {
    case STRIDE_MOVE_NONE:
      break;

    case STRIDE_MOVE_STEP_FWD:
      if (n->move_value > total - pos) {
        goto fail;
      }
      pos += n->move_value;
      break;

    case STRIDE_MOVE_STEP_BACK:
      if (n->move_value > pos) {
        goto fail;
      }
      pos -= n->move_value;
      break;

    case STRIDE_MOVE_ABS_HEAD:
      if (n->move_value > total) {
        goto fail;
      }
      pos = n->move_value;
      break;

    case STRIDE_MOVE_ABS_END:
      if (n->move_value > total) {
        goto fail;
      }
      pos = total - n->move_value;
      break;

    case STRIDE_MOVE_FIND_FWD:
    case STRIDE_MOVE_FIND_REV: {
      size_t hit;
      int found =
          (n->move == STRIDE_MOVE_FIND_FWD)
              ? seg_find_fwd(seg, total, pos, &n->move_target, &hit)
              : seg_find_rev(seg, pos, &n->move_target, &hit);
      if (!found) {
        goto fail;
      }
      pos = hit;
      break;
    }
    }

    /* ---- 2. 动作 ---- */
    switch (n->act) {
    case STRIDE_ACT_NONE:
      break;

    case STRIDE_ACT_COMPARE: {
      size_t es = n->act_target.len;
      if (es > total - pos) {
        goto fail;
      }
      if (memcmp(seg + pos, n->act_target.data, es) != 0) {
        goto fail;
      }
      pos += es;
      break;
    }

    case STRIDE_ACT_CAPTURE_BYTES: {
      if (!params || n->act_value > total - pos) {
        goto fail;
      }
      if (out_idx >= param_capacity) {
        goto fail;
      }
      params[out_idx].ptr = seg + pos;
      params[out_idx].len = n->act_value;
      out_idx++;
      pos += n->act_value;
      break;
    }

    case STRIDE_ACT_CAPTURE_UNTIL: {
      if (!params) {
        goto fail;
      }
      size_t start = pos;
      size_t end = total;
      size_t hit;
      if (seg_find_fwd(seg, total, start, &n->act_target, &hit)) {
        end = hit;
        pos = hit; /* 捕获后停在定界符前，不越过 */
      } else {
        pos = total; /* 未找到则捕获到段尾 */
      }
      if (out_idx >= param_capacity) {
        goto fail;
      }
      params[out_idx].ptr = seg + start;
      params[out_idx].len = end - start;
      out_idx++;
      break;
    }

    case STRIDE_ACT_CAPTURE_END: {
      if (!params) {
        goto fail;
      }
      if (out_idx >= param_capacity) {
        goto fail;
      }
      params[out_idx].ptr = seg + pos;
      params[out_idx].len = total - pos;
      out_idx++;
      pos = total;
      break;
    }
    }
    continue;

  fail:
    return -(int)(i + 1);
  }

  /* 段尾对齐 */
  if (pos != total) {
    return -(int)(seq->count + 1);
  }
  if (param_count) {
    *param_count = out_idx;
  }
  return 0;
}

/* ==================== 查找辅助实现 ==================== */

static int seg_find_fwd(const unsigned char *seg, size_t total, size_t from,
                        const stride_blob_t *target, size_t *hit) {
  if (!target || target->len == 0 || !seg || !hit) {
    return 0;
  }
  if (from >= total) {
    return 0;
  }
  if (target->len > total - from) {
    return 0;
  }

  for (size_t k = from; target->len <= total - k; k++) {
    if (memcmp(seg + k, target->data, target->len) == 0) {
      *hit = k;
      return 1;
    }
  }
  return 0;
}

static int seg_find_rev(const unsigned char *seg, size_t from,
                        const stride_blob_t *target, size_t *hit) {
  if (!target || target->len == 0 || !seg || !hit) {
    return 0;
  }
  /* from 是 exclusive 右边界，目标须完整落在 [0, from) 内 */
  if (from < target->len)
    return 0;

  size_t k = from - target->len; /* 最右合法起点 */

  for (;;) {
    if (memcmp(seg + k, target->data, target->len) == 0) {
      *hit = k;
      return 1;
    }
    if (k == 0)
      break;
    k--;
  }
  return 0;
}
