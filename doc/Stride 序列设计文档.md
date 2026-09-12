# Stride 序列设计文档

**文档版本**：1.0
**更新日期**：2026-09-11
**适用模块**：`stride/types.h`、`stride/sequence.h`、`stride/matcher.h`、`stride/extractor.h`
**适用版本**：Stride 3.0.0

---

> **阅读须知**：本文档描述 2026-09-11 重构后的 **v3 架构**。v2 的「操作符序列 → 序列（数组）+ 状态机」编译模型与相应数据结构、API 名称均已废弃（v3 已废弃状态机），本文档不沿用它们。v3 中序列是**单链表**、构建是**函数式**的。
>
> **范围**：Stride **不含语法，也不含编译器**。把 `$'…'`、`${…}`、`$[…]` 这类模式翻译成 Stride 的构建函数调用，是调用方（例如 URLRouter）的职责。Stride 自身只提供「函数式序列构建 + 通用执行引擎」。

---

## 一、概述

### 1.1 定位

Stride 是一个用 C99 编写的**步进式比特串匹配 / 提取库**，零外部依赖。它把输入看作**不透明二进制**：既不知道输入是文本还是二进制，也不解释任何编码，更不关心调用方用什么分隔符切分输入。

「Stride」这个名字就是它的第一个原语：**步长**（每一步跨越多少**比特**）。

### 1.2 两个原语

整个库只建立在两个原语之上：

| 原语 | 回答的问题 | 定义 |
|------|-----------|------|
| **步长 stride** | 指针怎么走 | 每一步跨越的**比特数** `s ≥ 1`；比特偏移 = `步序号 × s` |
| **比特串 blob** | 比什么 | `stride_blob_t { const void *data; size_t bit_len; }`，`bit_len` 为**比特长度** |

由此，同一套机制可以覆盖多种输入形态：

| 输入形态 | 步长 | 说明 |
|---------|------|------|
| 字节流（UTF-8、任意二进制） | 8 | 最常见；一步一字节 |
| 半字节 / BCD | 4 | 一步 4 比特 |
| 位域、标志位 | 1 | 一步 1 比特，可做纯位级比对 |
| UTF-16 / UTF-32 码元 | 16 / 32 | 一步一个码元 |
| 定长记录 | 记录的比特长度 | 一步一条记录 |

### 1.3 与分隔符、编码无关

Stride 的**执行引擎不解释任何编码**，也不认识任何分隔符：

- **不切分输入**：把输入按 `/` 等分隔符切成「段（segment）」是调用方的职责；Stride 的每次执行只面对**一个段**（`stride_extract_run` / `stride_seq_run` 都是单段入口，多段由 `stride_full_extractor_t` 顺序组合）。
- **不识别字符**：所谓「比对字面量」就是比对一段比特串；`$'v'` 与 `$'中文'` 在 Stride 眼里没有区别，前者 8 比特、后者 24 比特（UTF-8）。
- **不理解变长编码**：UTF-8 的一个字符占几个字节，Stride 不知道也不判断；它只会按步走、按比特比。
- **查找是比特查找**：`FIND_*` 在**步对齐**的格点上滑动比特窗口做逐比特比较，与「词」或「字符」的概念无关。

由此带来的能力与代价，都在第九节「边界与限制」中明确列出。

### 1.4 序列：匹配与提取是同一结构

序列（`stride_seq_t`）把「在一个段里怎么走、到了之后做什么」表达成一个**单链表**，节点类型为 `stride_step_t`，每个节点表达：

```
先偏移（move）  →  再执行动作（act）
```

**匹配序列与提取序列是同一结构的不同用法**：

| 用法 | 节点内容 | 执行时 | 执行入口 |
|------|---------|--------|---------|
| **匹配** | 偏移 + `STRIDE_ACT_COMPARE` | 不带参数缓冲 | `stride_match_run()` |
| **提取** | 偏移 + 捕获动作 | 带参数缓冲 | `stride_extract_run()` / `stride_full_extractor_run()` |

两者共用**同一个通用执行引擎** `stride_seq_run()`。这是 v3 的核心简化：引擎只有一份，匹配与提取的差异只体现在「节点里放了什么动作」和「执行时是否给参数缓冲」。

---

## 二、单位与量纲

### 2.1 量纲表

单位混用是本库最容易出错的地方，下表是唯一权威约定：

| 量 | 出现位置 | 单位 | 说明 |
|----|---------|------|------|
| **步长** `stride` | `stride_seq_run()` / `stride_match_run()` / `stride_extract_run()` 的 `stride` 参数 | **比特/步** | `s ≥ 1`；传 `0` 视为 `1` |
| **段长** `segment_bit_len` | 各执行入口 | **比特** | `= 8 × 字节数` |
| **字面量长度** `bit_len` | `stride_blob_t.bit_len`、`literals` 参数 | **比特** | 用 `STRIDE_BITS(nbytes)` 从字节数换算 |
| **位置 / 偏移** `steps` | `stride_seq_step_fwd/back()`、`abs_head/end()`、`move_value`（步类） | **步** | 比特偏移 = `步 × 步长` |
| **`SKIP_BITS` 的 `move_value`** | `stride_seq_skip_bits()` | **比特** | 单位是比特，**与步长无关**（见 3.4） |
| **`CAPTURE_STEPS` 的 `act_value`** | `stride_seq_capture_steps()` | **步** | 捕获 `act_value × 步长` 个比特 |
| **参数长度** `bit_len` | `stride_param_t.bit_len` | **比特** | 引擎按 `步数 × 步长` 或「到段尾」计算 |
| **查找目标长度** | `stride_seq_find_fwd/rev()` 的 `target.bit_len` | **比特** | 正向查找要求是步长整数倍 |

换算宏：

```c
#define STRIDE_BITS(nbytes) ((size_t)(nbytes) * 8u)
```

例如 `STRIDE_BITS(4) == 32`；一个有 4 字节的段，其 `segment_bit_len` 为 32。

### 2.2 段内位置模型

段有不透明二进制内容 `segment` 与比特长度 `segment_bit_len`。引入步长 `s` 后：

| 概念 | 定义 |
|------|------|
| 段长（步） | `total = segment_bit_len / s` |
| 游标 `pos` | 当前所在的**步序号**，初值 `0`（段首） |
| 比特偏移 | `pos * s` |
| **HEAD** | 段首，即第 `0` 步；不变基准 |
| **END** | 段尾，即第 `total` 步；随段长变化 |

`total` 在运行时由 `segment_bit_len / s` 得出，构建期不需要知道它——同一份序列可以在不同长度、不同步长的段上执行。

### 2.3 对齐约束

| 约束 | 位置 | 违反后果 |
|------|------|---------|
| 段比特长度必须是步长的整数倍：`segment_bit_len % stride == 0` | 执行入口 | 直接失败（返回 `-1`） |
| 比对字面量必须是步长整数倍：`act_target.bit_len % stride == 0` | `STRIDE_ACT_COMPARE` | 该节点失败 |
| 正向查找目标必须是步长整数倍：`target.bit_len % stride == 0` | `STRIDE_MOVE_FIND_FWD` | 查找失败 |
| 反向查找目标必须是步长整数倍 | `STRIDE_MOVE_FIND_REV` | 查找失败 |
| `SKIP_BITS` 的比特数必须是步长整数倍：`move_value % stride == 0` | `STRIDE_MOVE_SKIP_BITS` | 该节点失败 |
| 捕获起点必须**字节对齐**：`pos × stride % 8 == 0` | 全部捕获动作 | 该节点失败 |

「序列本身与步长无关」正是这些**执行期**对齐检查存在的原因：序列只记录步数与比特长度，能否在某个步长下执行由运行期判定。

---

## 三、数据结构

### 3.1 比特串 `stride_blob_t`

```c
typedef struct {
    const void *data;
    size_t bit_len;
} stride_blob_t;
```

| 字段 | 含义 |
|------|------|
| `data` | 指向原始数据首字节 |
| `bit_len` | **比特长度**（不是字节数） |

`stride_blob_t` **本身不表达所有权**：它只是一个「指针 + 长度」的视图。所有权取决于它出现在哪里：

| 场合 | 所有权 |
|------|--------|
| 作为构建函数入参（`literal` / `target`） | 调用方拥有；构建时 Stride 会**复制**一份副本到节点里 |
| 作为节点字段 `move_target` / `act_target` | **节点拥有**，由 `stride_seq_free()` / `stride_seq_clear()` 释放 |
| 作为 `stride_param_t` 返回 | **不拥有**，零拷贝指向输入段内部 |

复制规则（`blob_dup`）：若 `src` 为 `NULL`、`src->data` 为 `NULL` 或 `src->bit_len == 0`，则节点内为空 blob（`data = NULL`、`bit_len = 0`）；否则申请 `(bit_len + 7) / 8` 字节并整字节 `memcpy`。

> **注意**：复制是**整字节**复制。若 `bit_len` 不是 8 的倍数，末尾字节中**超出 `bit_len` 的高位噪声会被一并拷入**，但引擎只按 `bit_len` 参与比对，这些位不影响结果。

### 3.2 参数 `stride_param_t`

```c
typedef struct {
    const void *ptr;
    size_t bit_len;
} stride_param_t;
```

零拷贝的「指针 + 比特长度」对，直接指向**输入段内部**。约定：**参数起始位置必须字节对齐**（比特偏移是 8 的整数倍），因此 `ptr` 指向包含该比特串的第一个字节。非字节对齐的捕获会失败，而不是被「移位打包」。

### 3.3 序列节点 `stride_step_t`

```c
typedef struct stride_step {
    /* 偏移 */
    stride_move_t move;
    size_t        move_value;
    stride_blob_t move_target;

    /* 动作 */
    stride_act_t  act;
    stride_blob_t act_target;
    size_t        act_value;

    struct stride_step *next;
} stride_step_t;
```

逐字段说明：

| 字段 | 类型 | 含义 |
|------|------|------|
| `move` | `stride_move_t` | 偏移类型；`STRIDE_MOVE_NONE` 表示本节点不移动（游标停在原处） |
| `move_value` | `size_t` | 偏移量。**单位为步**（`STEP_FWD` / `STEP_BACK` / `ABS_HEAD` / `ABS_END`），但 `SKIP_BITS` 时**单位为比特**；`FIND_*` 与 `NONE` 时未使用（构建函数一律写 `0`） |
| `move_target` | `stride_blob_t` | `FIND_FWD` / `FIND_REV` 的查找目标；**由本节点拥有**，`stride_seq_free()` 释放；其他 move 类型为空 blob |
| `act` | `stride_act_t` | 到位后要执行的动作；`STRIDE_ACT_NONE` 表示只移动、不做动作 |
| `act_target` | `stride_blob_t` | `COMPARE` 要比对的字面量、`CAPTURE_UNTIL` 的终止比特串；**由本节点拥有**；其余 act 类型为空 blob |
| `act_value` | `size_t` | **单位为步**，仅 `CAPTURE_STEPS` 使用（捕获 `act_value` 步）；其余 act 类型未使用（构建函数写 `0`） |
| `next` | `struct stride_step *` | 单链表后继；尾节点为 `NULL` |

### 3.4 偏移枚举 `stride_move_t`

```c
typedef enum {
    STRIDE_MOVE_NONE = 0,
    STRIDE_MOVE_STEP_FWD,
    STRIDE_MOVE_STEP_BACK,
    STRIDE_MOVE_ABS_HEAD,
    STRIDE_MOVE_ABS_END,
    STRIDE_MOVE_SKIP_BITS,
    STRIDE_MOVE_FIND_FWD,
    STRIDE_MOVE_FIND_REV
} stride_move_t;
```

| 枚举值 | `move_value` 单位 | 语义 | 失败条件（执行期） |
|--------|------------------|------|------------------|
| `STRIDE_MOVE_NONE` | — | 不移动，游标保持 | 从不失败 |
| `STRIDE_MOVE_STEP_FWD` | **步** | 向段尾走 `move_value` 步 | `move_value > total - pos`（越出段尾） |
| `STRIDE_MOVE_STEP_BACK` | **步** | 向段首走 `move_value` 步 | `move_value > pos`（越出段首） |
| `STRIDE_MOVE_ABS_HEAD` | **步** | 定位到第 `move_value` 步（`HEAD + n`，绝对位置） | `move_value > total` |
| `STRIDE_MOVE_ABS_END` | **步** | 定位到 `END − move_value` 步（`move_value == 0` 即段尾） | `move_value > total` |
| `STRIDE_MOVE_SKIP_BITS` | **比特** | 向段尾走 `move_value` **比特**（内部换算成 `move_value / stride` 步） | `move_value % stride != 0`，或换算后的步数越出段尾 |
| `STRIDE_MOVE_FIND_FWD` | —（用 `move_target`） | 从当前位置起**向段尾**查找 `move_target`，落点为目标首步 | 目标为空/长度非步长整数倍/查不到 |
| `STRIDE_MOVE_FIND_REV` | —（用 `move_target`） | 从当前位置起**向段首**查找 `move_target`，落点为目标首步 | 同上 |

关于单位的**唯一例外**：`SKIP_BITS` 的 `move_value` 是比特，与其他四种「以步计」的偏移**单位不同**。它存在的意义是「跳过一段已验证的字面量」——调用方从字节长度出发，用 `STRIDE_BITS(n)` 表达最自然，不必先除以步长。也正因为单位不同，它**从不与步类偏移合并**（见 4.3）。

查找的落点语义（`seg_find_fwd` / `seg_find_rev`）：查找是**步对齐**的——窗口只在步的整数倍格点上滑动，落点 `hit` 记录目标首步，随后 `pos = hit`。`FIND_FWD` 从 `pos` 起向尾部扫描；`FIND_REV` 从 `pos - 1` 起（`pos == 0` 时从 `total - 1` 起）向头部扫描。

### 3.5 动作枚举 `stride_act_t`

```c
typedef enum {
    STRIDE_ACT_NONE = 0,
    STRIDE_ACT_COMPARE,
    STRIDE_ACT_CAPTURE_STEPS,
    STRIDE_ACT_CAPTURE_UNTIL,
    STRIDE_ACT_CAPTURE_END
} stride_act_t;
```

| 枚举值 | 使用字段 | 语义 | 失败条件（执行期） |
|--------|---------|------|------------------|
| `STRIDE_ACT_NONE` | — | 到位后不做任何事 | 从不失败 |
| `STRIDE_ACT_COMPARE` | `act_target` | 在游标处比对 `act_target`，成功后游标前进 `act_target.bit_len / stride` 步 | 长度非步长整数倍、越出段尾、比特不等 |
| `STRIDE_ACT_CAPTURE_STEPS` | `act_value` | 捕获 `act_value` **步**（即 `act_value × stride` 比特）并前移 | `params == NULL`、越出段尾、起点非字节对齐、参数容量不足 |
| `STRIDE_ACT_CAPTURE_UNTIL` | `act_target` | 捕获从游标到 `act_target` **首次出现位置之前**的比特；未找到则捕获到段尾。游标移到 `act_target` 首步或段尾 | `params == NULL`、起点非字节对齐、参数容量不足 |
| `STRIDE_ACT_CAPTURE_END` | — | 捕获从游标到**段尾**的全部剩余比特，游标移到段尾 | `params == NULL`、起点非字节对齐、参数容量不足 |

只有 `COMPARE` 与 `NONE` **不计入**参数个数；三个捕获动作都会使 `stride_seq_t.param_count` 递增一次（见 5.3）。

### 3.6 序列 `stride_seq_t`

```c
typedef struct {
    stride_step_t *head;
    stride_step_t *tail;
    size_t         count;       /* 节点数 */
    size_t         param_count; /* 捕获动作个数（提取序列用） */
} stride_seq_t;
```

| 字段 | 含义 |
|------|------|
| `head` | 链表头节点；空序列为 `NULL` |
| `tail` | 链表尾节点，构建时所有追加/合并都发生在它身上；空序列为 `NULL` |
| `count` | 节点数，`stride_seq_count()` 返回它 |
| `param_count` | **捕获动作个数**的静态计数（构建期累加），`stride_seq_param_count()` 返回它；用于调用方预估参数缓冲大小 |

### 3.7 状态码 `stride_status_t`

```c
typedef enum {
    STRIDE_OK = 0,
    STRIDE_E_INVALID_PATTERN,
    STRIDE_E_EMPTY_SEGMENT,
    STRIDE_E_ALIGN,
    STRIDE_E_NOMEM
} stride_status_t;
```

| 枚举值 | 字符串（`stride_status_str`） | 含义 |
|--------|------------------------------|------|
| `STRIDE_OK` | `"ok"` | 成功 |
| `STRIDE_E_INVALID_PATTERN` | `"invalid pattern"` | 模式格式无效 |
| `STRIDE_E_EMPTY_SEGMENT` | `"empty segment"` | 空的段模式 |
| `STRIDE_E_ALIGN` | `"alignment error"` | 步长对齐错误 |
| `STRIDE_E_NOMEM` | `"out of memory"` | 内存分配失败 |

该枚举用于**调用方自身的模式处理流程**（例如 URLRouter 在把模式翻译成构建调用时报告错误）。Stride 的序列构建与执行 API 一律使用 `0` / `-1` 与负数失败码，见第五、六节。

---

## 四、单链表与尾部合并

### 4.1 构建是函数式追加

序列的构建**没有状态机**：每一次构建调用只做一件事——尝试把新的偏移或动作**追加到尾节点**，能就地合并就合并，否则新建一个尾节点。

```
stride_seq_new()                     head = tail = NULL, count = 0
stride_seq_step_fwd(seq, 3)          ┌──────────────┐
                                     │ FWD 3 / NONE │ → NULL        count = 1
                                     └──────────────┘
stride_seq_compare(seq, &lit)        ┌──────────────┐
                                     │ FWD 3 / CMP  │ → NULL        count = 1（绑定）
                                     └──────────────┘
stride_seq_find_fwd(seq, &dot)       ┌──────────────┐   ┌──────────────┐
                                     │ FWD 3 / CMP  │ → │ FIND_FWD dot │ → NULL   count = 2
                                     └──────────────┘   └──────────────┘
```

「函数式」体现在：调用序列**就是**构建结果，调用顺序决定节点顺序，构建器不保留任何中间状态（没有待定操作、没有模式缓冲），也不需要在末尾「提交」。

### 4.2 追加偏移的规则

`seq_add_move(seq, kind, value, target)` 的判定顺序：

1. 若 `seq == NULL` → 返回 `-1`。
2. 令 `t = seq->tail`。**只有当 `t != NULL`、`t->act == STRIDE_ACT_NONE`、且 `target == NULL`（非 `FIND_*`）时**才尝试合并。
3. `move_try_merge(t, kind, value)` 返回 `1` 表示已就地合并，直接返回 `0`。
4. 否则 `seq_push()` 新建尾节点，写入 `move` / `move_value`，并复制 `target`（无 target 时为空 blob）。分配失败返回 `-1`。

**为什么尾节点已有动作时不能合并**：节点语义是「先偏移、再动作」，动作是相对**该节点偏移之后**的游标位置执行的。若在已带动作的节点上继续叠加偏移，就会改变那个动作的执行位置，语义被破坏。因此必须新建节点——新节点由 `calloc` 置零（`move = STRIDE_MOVE_NONE`、`act = STRIDE_ACT_NONE`），本次的偏移写入它的偏移槽，随后到来的动作又能正常绑定到它身上。

> **注意**：这条限制针对的是「尾节点**已经带了动作**」，而不是「尾节点的偏移槽已被别的偏移占用」——偏移槽能否复用由 `move_try_merge` 判定（见 4.3）。所以「尾节点有动作 → 一定新建节点」，反之「尾节点没动作 → 尝试合并，合并失败也新建节点」。8.2 节的实例展示了这条规则带来的实际节点形态。

### 4.3 合并规则表

`move_try_merge` 是唯一事实来源，逐条如下（「已有」= 尾节点当前的 `move`，「新增」= 本次调用的 `kind`）：

| 已有 \ 新增 | `STEP_FWD` | `STEP_BACK` | `SKIP_BITS` | `FIND_FWD/REV` | `ABS_HEAD` / `ABS_END` / `NONE` |
|---|---|---|---|---|---|
| **`STEP_FWD`** | ✅ **相加**：`n += v` | ✅ **抵消**：`v ≤ n` 时 `n -= v`；否则**翻转**为 `STEP_BACK`，值 `v - n` | ❌ 不合并（单位不同） | ❌ 不合并（`FIND_*` 从不合并） | — |
| **`STEP_BACK`** | ✅ **抵消**：`v ≥ n` 时**翻转**为 `STEP_FWD`，值 `v - n`；否则 `n -= v` | ✅ **相加**：`n += v` | ❌ 不合并（单位不同） | ❌ 不合并 | — |
| **`ABS_HEAD`** | ✅ **相加**：`n += v` | ✅ 仅当 `v ≤ n` 时**相加（相减）**：`n -= v`；否则 ❌ 不合并 | ❌ 不合并（单位不同） | ❌ 不合并 | — |
| **`ABS_END`** | ❌ 不合并 | ✅ **相加**：`n += v` | ❌ 不合并（单位不同） | ❌ 不合并 | — |
| **`SKIP_BITS`** | ❌ 不合并（单位不同） | ❌ 不合并（单位不同） | ✅ **相加**：`n += v` | ❌ 不合并 | — |
| **`FIND_FWD` / `FIND_REV`** | ❌ 从不合并 | ❌ 从不合并 | ❌ 从不合并 | ❌ 从不合并 | — |
| **`NONE`**（新节点默认值） | ❌ `move_try_merge` 不处理 | ❌ | ❌ | ❌ | — |

> 表中 `n` 是尾节点已有的 `move_value`，`v` 是本次新增的值。`ABS_HEAD` / `ABS_END` / `NONE` 永远不会作为**新增**类型出现（构建函数只产生前六种偏移），故该列无意义。
>
> `move_target` 非空（即 `FIND_*`）时 `seq_add_move` 在第 2 步就放弃合并；即使侥幸走到 `move_try_merge`，其 `default` 分支也只返回「不可合并」。因此「`FIND_*` 从不合并」有两重保证。
>
> 尾节点 `move == NONE` 时本表所有「不合并」结论都成立——`move_try_merge` 的 `switch` 落到 `default`，返回 `0`，由调用方新建节点。

**为什么要合并**：连续步进是最常见的模式（`$[>3]$'x'`、`${4}$'-'` 等），把它们折叠成一个节点，能显著减少节点数和执行时的分支次数，同时让链表长度反映「真正的位置决策点数量」。

**为什么不同单位不合并**：`STEP_FWD` 的 `n` 是**步**，`SKIP_BITS` 的 `v` 是**比特**。构建期不知道步长（步长是执行期参数），`n` 步与 `v` 比特之间**无法换算**，因此既不能相加也不能抵消，只能各占一个节点，由执行期分别处理。

**为什么有些组合不合并**：

| 组合 | 原因 |
|------|------|
| `ABS_HEAD` + `STEP_BACK`（`v > n`） | 结果 `HEAD + n − v` 可能落在段首之前。`ABS_HEAD` 的 `move_value` 是无符号的「段首偏移」，无法表示为负；保持两个节点，让执行期按「先定位到 HEAD+n，再后退 v 步」判定失败，语义更直观 |
| `ABS_END` + `STEP_FWD` | 结果 `END − n + v` 越过段尾。`ABS_END` 只表达「离段尾多远」，无法表达「越过段尾」，故不合并 |
| `FIND_*` + 任何 | 查找落点由**段内容**决定，运行前不可知，无法与任何常量偏移折算 |
| 任何 + `FIND_*` | 同上：`move_target` 非空导致第 2 步直接放弃合并 |

### 4.4 追加动作的规则

`seq_add_act(seq, act, literal, value)` 的判定顺序：

1. 若 `seq == NULL` → 返回 `-1`。
2. 令 `n = seq->tail`。若 `n == NULL`（空序列）**或** `n->act != STRIDE_ACT_NONE`（尾节点已有动作），则 `seq_push()` 新建尾节点。
3. 写入 `n->act = act`、`n->act_value = value`，并把 `literal` 复制到 `n->act_target`（为 `NULL` 时为空 blob）。复制失败返回 `-1`。
4. 若 `act` 既不是 `STRIDE_ACT_NONE` 也不是 `STRIDE_ACT_COMPARE`，则 `seq->param_count++`。

由此得到两种典型模式：

| 调用序列 | 结果 |
|---------|------|
| 先偏移、再动作（`step_fwd(3)` → `compare(lit)`） | **绑定**到同一节点：`move = FWD 3`，`act = COMPARE lit`，`count` 不增加 |
| 动作、动作（`compare(a)` → `compare(b)`） | 第二个动作**新建**节点：`{NONE, COMPARE a} → {NONE, COMPARE b}`，`count` 增加 |
| 空序列上直接动作（`compare(a)`） | 新建节点：`{NONE, COMPARE a}` |

> 动作「绑定到尾节点」正是「一个节点 = 先在当前位置偏移、然后在该位置做个动作」这一语义的落点。`COMPARE` 在 `move` 为 `NONE` 时表示「就在当前位置比对」。

**动作与偏移的先后次序不影响结果**：`step_fwd(3)` → `compare(lit)` 与「空序列上 `compare(lit)`、再 `step_fwd(3)`」会得到不同形态的节点（前者 `{FWD 3, COMPARE}`，后者 `{NONE, COMPARE}` 后接 `{FWD 3, NONE}`），因为**构建顺序决定节点顺序**。想让偏移与动作落在同一节点上，就先调构建偏移的函数、再调构建动作的函数。

### 4.5 构建函数永不失败于「语义非法」

构建期**不做**段长、对齐、位置可达性检查——这些只有执行期才知道（段长与步长都是执行期参数）。构建函数只在两类情况下返回 `-1`：

- `seq == NULL`；
- 内存分配失败（新建节点或复制比特串时 `malloc` 失败）。

任何「能构建出来但执行时必然失败」的序列（例如连续三步 `STEP_BACK`、或 `ABS_HEAD 100` 而段只有 3 步）都是合法的构建结果。

---

## 五、构建 API

### 5.1 生命周期与查询

```c
stride_seq_t *stride_seq_new(void);
void          stride_seq_free(stride_seq_t *seq);
void          stride_seq_clear(stride_seq_t *seq);
size_t        stride_seq_count(const stride_seq_t *seq);
size_t        stride_seq_param_count(const stride_seq_t *seq);
```

| 函数 | 参数 | 语义 | 返回值 |
|------|------|------|--------|
| `stride_seq_new` | — | 分配一个空序列（`calloc`，所有字段为零） | 新序列；分配失败返回 `NULL` |
| `stride_seq_free` | `seq` | 释放全部节点、节点内两个比特串副本，再释放序列本身。`seq == NULL` 时无操作 | 无 |
| `stride_seq_clear` | `seq` | 释放全部节点与比特串副本，并把容器**归零复用**（`count = param_count = 0`，`head = tail = NULL`）。`seq == NULL` 时无操作 | 无 |
| `stride_seq_count` | `seq` | 节点数 | `seq == NULL` 时返回 `0` |
| `stride_seq_param_count` | `seq` | 构建期累计的**捕获动作个数** | `seq == NULL` 时返回 `0` |

`stride_seq_clear` 保留了 `stride_seq_t` 这块容器本身，适合「一次分配、反复重建」的使用方式；但**不会**回收已释放节点占用的堆内存。

### 5.2 偏移构建函数

```c
int stride_seq_step_fwd(stride_seq_t *seq, size_t steps);
int stride_seq_step_back(stride_seq_t *seq, size_t steps);
int stride_seq_abs_head(stride_seq_t *seq, size_t steps);
int stride_seq_abs_end(stride_seq_t *seq, size_t steps);
int stride_seq_skip_bits(stride_seq_t *seq, size_t bit_len);
int stride_seq_find_fwd(stride_seq_t *seq, const stride_blob_t *target);
int stride_seq_find_rev(stride_seq_t *seq, const stride_blob_t *target);
```

| 函数 | 参数 | 单位 | 语义 | 返回 |
|------|------|------|------|------|
| `stride_seq_step_fwd` | `steps` | 步 | 向段尾走 `steps` 步（`STRIDE_MOVE_STEP_FWD`） | `0` 成功 / `-1` 失败 |
| `stride_seq_step_back` | `steps` | 步 | 向段首走 `steps` 步（`STRIDE_MOVE_STEP_BACK`） | `0` / `-1` |
| `stride_seq_abs_head` | `steps` | 步 | 定位到第 `steps` 步（`HEAD + steps`） | `0` / `-1` |
| `stride_seq_abs_end` | `steps` | 步 | 定位到 `END − steps` 步；`0` 即段尾（常用于 `${}`） | `0` / `-1` |
| `stride_seq_skip_bits` | `bit_len` | **比特** | 向段尾走 `bit_len` 比特；提取阶段跳过已验证字面量的优化手段 | `0` / `-1` |
| `stride_seq_find_fwd` | `target` | — | 从当前位置向段尾查找 `target` 比特串，落点为其首步 | `0` / `-1` |
| `stride_seq_find_rev` | `target` | — | 从当前位置向段首查找 `target` 比特串，落点为其首步 | `0` / `-1` |

`target` 会被**复制**进节点（`blob_dup`），调用后调用方即可释放自己的数据；`target->bit_len` 应为其**比特长度**。

`stride_seq_find_fwd` 与 `stride_seq_find_rev` 的内部 `move_value` 一律写 `0`，查找目标只放在 `move_target` 中。

### 5.3 动作构建函数

```c
int stride_seq_compare(stride_seq_t *seq, const stride_blob_t *literal);
int stride_seq_capture_steps(stride_seq_t *seq, size_t steps);
int stride_seq_capture_until(stride_seq_t *seq, const stride_blob_t *target);
int stride_seq_capture_end(stride_seq_t *seq);
```

| 函数 | 参数 | 单位 | 语义 | `param_count` | 返回 |
|------|------|------|------|--------------|------|
| `stride_seq_compare` | `literal` | — | 在游标处比对 `literal` 比特串并前进其步数 | 不变 | `0` / `-1` |
| `stride_seq_capture_steps` | `steps` | 步 | 捕获 `steps` 步（`steps × 步长` 比特） | **+1** | `0` / `-1` |
| `stride_seq_capture_until` | `target` | — | 捕获到 `target` 首次出现之前；未找到则捕获到段尾 | **+1** | `0` / `-1` |
| `stride_seq_capture_end` | — | — | 捕获到段尾 | **+1** | `0` / `-1` |

`literal` 与 `target` 均会被复制进节点（`capture_end` 无目标）。所有函数返回 `0` 表示成功、`-1` 表示失败（原因同 4.5）。

### 5.4 构建 API 全表

| 函数 | 类别 | 关键参数（单位） | 节点字段落点 |
|------|------|----------------|------------|
| `stride_seq_new` | 生命周期 | — | 分配 `stride_seq_t` |
| `stride_seq_free` | 生命周期 | `seq` | 释放全部 |
| `stride_seq_clear` | 生命周期 | `seq` | 释放节点、容器归零 |
| `stride_seq_count` | 查询 | `seq` | 读 `count` |
| `stride_seq_param_count` | 查询 | `seq` | 读 `param_count` |
| `stride_seq_step_fwd` | 偏移 | `steps`（步） | `move = FWD`，`move_value` |
| `stride_seq_step_back` | 偏移 | `steps`（步） | `move = BACK`，`move_value` |
| `stride_seq_abs_head` | 偏移 | `steps`（步） | `move = ABS_HEAD`，`move_value` |
| `stride_seq_abs_end` | 偏移 | `steps`（步） | `move = ABS_END`，`move_value` |
| `stride_seq_skip_bits` | 偏移 | `bit_len`（**比特**） | `move = SKIP_BITS`，`move_value` |
| `stride_seq_find_fwd` | 偏移 | `target`（比特串） | `move = FIND_FWD`，`move_target` |
| `stride_seq_find_rev` | 偏移 | `target`（比特串） | `move = FIND_REV`，`move_target` |
| `stride_seq_compare` | 动作 | `literal`（比特串） | `act = COMPARE`，`act_target` |
| `stride_seq_capture_steps` | 动作 | `steps`（步） | `act = CAPTURE_STEPS`，`act_value` |
| `stride_seq_capture_until` | 动作 | `target`（比特串） | `act = CAPTURE_UNTIL`，`act_target` |
| `stride_seq_capture_end` | 动作 | — | `act = CAPTURE_END` |

---

## 六、通用执行引擎 `stride_seq_run()`

### 6.1 原型与参数

```c
int stride_seq_run(const stride_seq_t *seq, size_t stride, const void *segment,
                   size_t segment_bit_len, stride_param_t *params,
                   size_t param_capacity, size_t *param_count);
```

| 参数 | 入/出 | 语义 |
|------|------|------|
| `seq` | 入 | 步进序列；`NULL` → 返回 `-1` |
| `stride` | 入 | 执行期步长（**比特/步**）；`0` 视为 `1` |
| `segment` | 入 | 段数据（不透明二进制）；`NULL` → 返回 `-1` |
| `segment_bit_len` | 入 | 段**比特长度**；必须是 `stride` 的整数倍 |
| `params` | 出 | 参数缓冲；**`NULL` 表示纯匹配**（遇到捕获动作即失败） |
| `param_capacity` | 入 | `params` 容量（可写参数个数上限） |
| `param_count` | 入/出 | 入参：已写入参数个数（起始下标）；出参：执行后的总数。可为 `NULL`（此时按 `0` 起算且不回写） |

### 6.2 执行流程

```
        ┌──────────────────────────────────────────────┐
        │ 前置检查                                      │
        │  seq / segment 非空；stride==0 → 1            │
        │  segment_bit_len % stride == 0                │
        └───────────────────────┬──────────────────────┘
                                ▼
        total = segment_bit_len / stride ; pos = 0 ; i = 0
                                │
        ┌───────────────────────▼──────────────────────┐
        │ 对每个节点 n（i = 0,1,…,count-1）：            │
        │   ① 执行偏移  →  更新 pos                     │
        │   ② 执行动作  →  比对 / 写 params / 移动 pos   │
        │   失败 → return -(i+1)                        │
        └───────────────────────┬──────────────────────┘
                                ▼
        pos != total  → return -(count+1)     （段尾未对齐）
                                ▼
        *param_count = 写入后的总数 ;  return 0
```

### 6.3 前置检查

| 检查 | 失败返回 |
|------|---------|
| `seq == NULL` 或 `segment == NULL` | `-1` |
| `stride == 0` | 视为 `1`（不是错误） |
| `segment_bit_len % stride != 0` | `-1` |

段长不是步长整数倍时**直接失败**，这是最外层、最廉价的护栏。`stride == 0` 视为 `1` 是一个便捷约定，便于「1 比特一步」的场景少写参数。

### 6.4 逐节点「偏移 → 动作」

节点的执行顺序**严格固定**：先偏移、后动作。同一个节点里的 `move` 与 `act` 是「先走再做事」的关系，二者共享同一个游标 `pos`。

**偏移阶段**（按 `move` 分支）：

| `move` | 执行 | 失败条件（`goto fail` → `-(i+1)`） |
|--------|------|--------------------------------|
| `NONE` | 无操作 | 无 |
| `STEP_FWD` | `pos += move_value` | `move_value > total - pos` |
| `STEP_BACK` | `pos -= move_value` | `move_value > pos` |
| `ABS_HEAD` | `pos = move_value` | `move_value > total` |
| `ABS_END` | `pos = total - move_value` | `move_value > total` |
| `SKIP_BITS` | 先查 `move_value % stride == 0`，再 `pos += move_value / stride` | 非步长整数倍；或换算后的步数 `> total - pos` |
| `FIND_FWD` | 从 `pos` 向段尾找 `move_target`，`pos = hit` | 查不到（含目标为空、长度非步长整数倍） |
| `FIND_REV` | 从 `pos - 1`（`pos == 0` 时从 `total - 1`）向段首找，`pos = hit` | 查不到；段长为 0 |

**动作阶段**（按 `act` 分支）：

| `act` | 执行 | 失败条件 |
|-------|------|---------|
| `NONE` | 无操作 | 无 |
| `COMPARE` | 查 `act_target.bit_len % stride == 0`；步数 `es = bit_len / stride`；在比特偏移 `pos × stride` 处逐比特（字节对齐时用 `memcmp`）比对 `act_target`；`pos += es` | 长度非步长整数倍；`es > total - pos`；比特不相等 |
| `CAPTURE_STEPS` | `params[out_idx] = { seg + start_bit/8, act_value × stride }`；`out_idx++`；`pos += act_value` | `params == NULL`；`act_value > total - pos`；起点非字节对齐；`out_idx >= param_capacity` |
| `CAPTURE_UNTIL` | 从 `pos` 起查找 `act_target`，`end = hit`（未找到则 `end = total`）；写 `{ seg + start_bit/8, (end − pos) × stride }`；`pos = end` | `params == NULL`；起点非字节对齐；`out_idx >= param_capacity` |
| `CAPTURE_END` | 写 `{ seg + pos×stride/8, segment_bit_len − pos×stride }`；`pos = total` | `params == NULL`；起点非字节对齐；`out_idx >= param_capacity` |

其中 `start_bit = pos × stride` 是捕获起点相对段首的比特偏移；`out_idx` 的初值是 `param_count ? *param_count : 0`，因此 `param_count` 可用于在**同一次执行内跨序列**把参数连续追加到同一缓冲（多段提取正是靠这一点，见 7.4）。

### 6.5 比对实现

`COMPARE` 走 `bits_eq(seg, pos * stride, act_target.data, act_target.bit_len)`：

- 若比特偏移与比特长度**都是 8 的整数倍**，直接用 `memcmp` 比较 `bit_len / 8` 字节；
- 否则退化为逐比特比较，比特序为 **MSB 优先**（`bit_at` 取 `(base[bit >> 3] >> (7 - (bit & 7))) & 1`）；
- `bit_len == 0` 视为相等（空字面量恒真）。

查找（`FIND_*`）与 `CAPTURE_UNTIL` 内部都复用 `bits_eq`，因此它们的比较语义与 `COMPARE` 完全一致。

### 6.6 段尾对齐要求

**无论匹配还是提取**，执行结束（所有节点走完）时都要求：

```c
pos == total        /* 游标恰好落在段尾 */
```

否则返回 `-(count + 1)`。

这条要求是「整段模式」的语义体现：序列描述的是**整个段**的形态，而不是段内某个子串。因此：

- 只走了一部分就结束 → 失败；
- 走过头（越出段尾）→ 更早在偏移阶段就失败（`-(i+1)`），而不会等到段尾检查。

「纯偏移节点」因此也有实际意义：`stride_seq_step_fwd(m, 3)` 单独执行时，成功**当且仅当**段恰好是 3 步——它同时充当步数与段长的校验。

### 6.7 返回值约定

| 返回值 | 含义 |
|--------|------|
| `0` | 成功（且 `pos == total`） |
| `-(i + 1)` | 第 `i` 个节点失败（`i` 从 `0` 起），即 `-1` 表示第 0 个节点失败、`-2` 表示第 1 个…… |
| `-(count + 1)` | 全部节点成功但**段尾未对齐**（`pos != total`） |
| `-1`（前置检查） | `seq == NULL` 或 `segment == NULL`，或段长不是步长整数倍 |

`stride_match_run()` 与 `stride_extract_run()` 会把任何负值统一压成 `-1`；需要定位失败节点时应直接调用 `stride_seq_run()`。

### 6.8 `params == NULL` 表示纯匹配

当 `params == NULL` 时：

- 所有 `CAPTURE_*` 动作**立即失败**（返回该节点的 `-(i+1)`）；
- `COMPARE` 与偏移不受影响。

这正是 `stride_match_run()` 的实现方式：以「空参数缓冲」执行序列。于是**同一个序列**既可以当匹配序列用（只关心是否走通），也可以当提取序列用（同时收参数）——只要它不含捕获动作。

`param_count` 也可以为 `NULL`：此时起始下标按 `0` 计算，执行后不回写总数。

### 6.9 参数零拷贝与字节对齐

参数以 `stride_param_t { const void *ptr; size_t bit_len; }` 返回，`ptr` **直接指向输入段的内部**，没有任何拷贝。代价是一条硬约束：

> 捕获起始的**比特偏移必须是 8 的整数倍**（`pos × stride % 8 == 0`），否则捕获失败。

原因很直接：`ptr` 只能指向**字节**，无法表达「从某字节的第 3 个比特开始」这种非字节对齐的起点。引擎选择**显式失败**而不是静默移位打包，避免调用方拿到看似正确、实则错位的数据。

不同步长下的对齐后果：

| 步长 | 只在哪些 `pos` 上可捕获 | 说明 |
|------|----------------------|------|
| 8 | 任意 `pos` | `pos × 8` 恒为 8 的倍数 |
| 4 | 偶数 `pos` | 半字节对齐才能落到字节边界 |
| 16 / 32 | 任意 `pos` | 码元天然字节对齐 |
| 1 | `pos` 为 8 的倍数 | 比特级序列需注意 |
| 其他（如 3、5、12） | 使 `pos × stride` 为 8 的倍数的 `pos` | 由 `pos` 与步长的组合决定 |

---

## 七、匹配用法与提取用法

### 7.1 匹配：`stride_match_run()`

```c
int stride_match_run(const stride_seq_t *seq, size_t stride,
                     const void *segment, size_t segment_bit_len);
```

| 参数 | 语义 |
|------|------|
| `seq` | 匹配序列，**只应含偏移与 `STRIDE_ACT_COMPARE`** |
| `stride` | 执行期步长（比特/步）；`0` 视为 `1` |
| `segment` | 段数据 |
| `segment_bit_len` | 段比特长度 |

**返回**：`0` 匹配成功；负数表示失败（`-1` 表示一般失败或第 0 个节点失败，其余负值表示第 `|r|-1` 个节点失败，或段尾未对齐）。

实现就是一次「不带参数缓冲」的通用执行：

```c
return stride_seq_run(seq, stride, segment, segment_bit_len, NULL, 0, NULL);
```

匹配序列的两种构造方式：

| 希望表达 | 构造方式 |
|---------|---------|
| 在当前位置比对字面量 | `stride_seq_compare(seq, &lit)` |
| 先走 `n` 步再比对 | `stride_seq_step_fwd(seq, n)` 然后 `stride_seq_compare(seq, &lit)`（自动绑定到同一节点） |
| 跳到某处再比对 | `stride_seq_abs_head/abs_end`、`stride_seq_find_fwd/rev` 之后 `compare` |
| 只校验段长 / 丢弃一段 | 纯偏移节点，不接动作 |

匹配序列里**不应出现**捕获动作：在 `params == NULL` 下它们必然失败。

### 7.2 提取：`stride_extract_run()`

```c
typedef stride_seq_t stride_extractor_t;

int stride_extract_run(const stride_extractor_t *ex, size_t stride,
                       const void *segment, size_t segment_bit_len,
                       stride_param_t *params, size_t param_capacity,
                       size_t *param_count);
```

| 参数 | 语义 |
|------|------|
| `ex` | 提取序列（与步进序列**同构**，`typedef` 别名） |
| `stride` | 执行期步长（比特/步）；`0` 视为 `1` |
| `segment` / `segment_bit_len` | 段数据与段比特长度 |
| `params` | 参数数组（**不可为 `NULL`**） |
| `param_capacity` | 参数数组容量 |
| `param_count` | 入参：已写入的参数数；出参：写入后的参数总数（**不可为 `NULL`**） |

**返回**：`0` 成功；`-1` 失败。

实现是对 `stride_seq_run()` 的封装：先检查 `params` 与 `param_count` 非空，再执行，任何非零结果都压成 `-1`。

```c
if (!params || !param_count) {
    return -1;
}
return stride_seq_run(ex, stride, segment, segment_bit_len, params,
                      param_capacity, param_count) == 0
           ? 0
           : -1;
```

调用方的典型流程：

```c
size_t n = 0;                                     /* 起始下标 0 */
if (stride_extract_run(e, 8, seg, STRIDE_BITS(len), p, cap, &n) == 0) {
    /* p[0..n-1] 有效，均为零拷贝视图 */
}
```

`stride_seq_param_count(e)` 给出该序列声明的捕获动作个数，可用来预估 `cap`。

### 7.3 提取参数的产生顺序

参数按**节点顺序**、也就是按动作在序列中出现的先后依次写入：每遇到一个 `CAPTURE_*` 动作写一个 `stride_param_t` 并使下标前进 1。`COMPARE` 与纯偏移不产生参数。

例如 `${4}$'-'${2}$'-'${2}`：三个捕获动作，产生三个参数；两个 `SKIP_BITS` 不产生参数。

### 7.4 多段提取：`stride_full_extractor_t`

```c
typedef struct {
    stride_extractor_t **segments;
    size_t               segment_count;
    size_t               total_params;
} stride_full_extractor_t;

stride_full_extractor_t *stride_full_extractor_create(
    stride_extractor_t **seg_extractors, size_t segment_count);

void stride_full_extractor_destroy(stride_full_extractor_t *full);

int stride_full_extractor_run(const stride_full_extractor_t *full, size_t stride,
                              const void *const *segments,
                              const size_t *seg_bit_lens, size_t segment_count,
                              stride_param_t *params, size_t param_capacity,
                              size_t *out_count);
```

**`create`**

| 参数 | 语义 |
|------|------|
| `seg_extractors` | 每段一个提取序列的**指针数组** |
| `segment_count` | 段数，必须 `> 0` |
| 返回 | 新建的完整提取器；`seg_extractors == NULL`、`segment_count == 0` 或分配失败时返回 `NULL` |

构造行为：

- **只复制指针数组**，不复制传入的 `seg_extractors` 数组本身，也不接管该数组所有权的释放；
- 但会把数组中的**每个提取序列的指针**保存下来，`total_params = Σ stride_seq_param_count(seg_extractors[i])`。

**`destroy`**

释放 `full->segments` 数组、**释放其中每一个提取序列**（`stride_seq_free`），再释放 `full` 自身。`NULL` 时无操作。

> **所有权提示**：`create` 复制指针数组、`destroy` 释放序列——因此 `seg_extractors[i]` 交给完整提取器后，调用方**不应**再自行释放同一序列，也不应在 `destroy` 之后继续使用。

**`run`**

| 参数 | 语义 |
|------|------|
| `full` | 完整提取器 |
| `stride` | 执行期步长（**所有段共用**） |
| `segments` | 段数据数组 |
| `seg_bit_lens` | 每段比特长度数组（**不可为 `NULL`**） |
| `segment_count` | 段数；必须等于 `full->segment_count`，否则失败 |
| `params` | 参数数组 |
| `param_capacity` | 参数数组容量（所有段共用） |
| `out_count` | 输出参数总数 |
| 返回 | `0` 成功；`-1` 失败 |

执行语义：

1. 任一必需指针为 `NULL` → `-1`；
2. `full->segment_count != segment_count` → `-1`；
3. 依段顺序对每个非 `NULL` 的提取序列调用 `stride_extract_run()`，并在调用间**保持 `param_idx` 连续**——因此**参数按段顺序连接**写入同一数组：第 0 段的参数在前，第 1 段的参数紧随其后；
4. 任一段失败 → 立即返回 `-1`（已写入的参数不在契约内，调用方不应使用）；
5. 全部成功 → `*out_count = param_idx`，返回 `0`。

`full->segments[i] == NULL` 的槽位会被跳过（不产生参数，也不失败）。

---

## 八、完整示例

以下代码与真实 API 一致，使用 `#include "stride/stride.h"`（它一并包含 `types.h`、`sequence.h`、`matcher.h`、`extractor.h`）。

```c
#include <stdio.h>
#include <string.h>

#include "stride/stride.h"

/* 由 C 字符串构造比特串视图（注意：单位是比特） */
static stride_blob_t blob(const char *s) {
    stride_blob_t b;
    b.data = s;
    b.bit_len = STRIDE_BITS(strlen(s));
    return b;
}
```

### 8.1 匹配：手工构建 `$'v'${'.'}$'.'${}`（步长 8）并匹配 `"v2.0"`

```c
#include <stdio.h>
#include <string.h>

#include "stride/stride.h"

static stride_blob_t blob(const char *s) {
    stride_blob_t b;
    b.data = s;
    b.bit_len = STRIDE_BITS(strlen(s));
    return b;
}

int main(void) {
    stride_seq_t *m = stride_seq_new();
    stride_blob_t v = blob("v");
    stride_blob_t dot = blob(".");

    stride_seq_compare(m, &v);    /* $'v'   —— 在段首比对 "v"（8 比特 = 1 步） */
    stride_seq_find_fwd(m, &dot); /* ${'.'} —— 匹配阶段：向段尾查找 "." */
    stride_seq_compare(m, &dot);  /* $'.'   —— 绑到上一个节点，在落点比对 "." */
    stride_seq_abs_end(m, 0);     /* ${}    —— 游标移到段尾，满足段尾对齐 */

    printf("nodes = %zu, params = %zu\n",
           stride_seq_count(m), stride_seq_param_count(m)); /* nodes = 3, params = 0 */

    printf("match \"v2.0\" -> %s\n",
           stride_match_run(m, 8, "v2.0", STRIDE_BITS(4)) == 0 ? "hit" : "miss");
    printf("match \"v2\"   -> %s\n",
           stride_match_run(m, 8, "v2", STRIDE_BITS(2)) == 0 ? "hit" : "miss");
    printf("match \"x2.0\" -> %s\n",
           stride_match_run(m, 8, "x2.0", STRIDE_BITS(4)) == 0 ? "hit" : "miss");

    stride_seq_free(m);
    return 0;
}
```

构建出的链表（`count == 3`）：

| 节点 | `move` | `move_value` | `move_target` | `act` | `act_target` |
|------|--------|-------------|--------------|-------|-------------|
| 0 | `STRIDE_MOVE_NONE` | 0 | 空 | `STRIDE_ACT_COMPARE` | `"v"`（8 比特） |
| 1 | `STRIDE_MOVE_FIND_FWD` | 0 | `"."`（8 比特） | `STRIDE_ACT_COMPARE` | `"."`（8 比特） |
| 2 | `STRIDE_MOVE_ABS_END` | 0 | 空 | `STRIDE_ACT_NONE` | 空 |

执行 `"v2.0"`（`total = 4`）的过程：

| 节点 | 偏移 | 动作 | 执行后 `pos` |
|------|------|------|-------------|
| 0 | `NONE`：不动 | `COMPARE "v"`：比特偏移 0 处等于 `"v"`，前进 1 步 | 1 |
| 1 | `FIND_FWD "."`：从第 1 步起找到 `"."` 在第 2 步 | `COMPARE "."`：比特偏移 16 处等于 `"."`，前进 1 步 | 3 |
| 2 | `ABS_END 0`：`pos = 4 − 0 = 4` | `NONE` | 4 |

`pos == total == 4`，段尾对齐，`stride_match_run()` 返回 `0`（命中）。

`"v2"` 的情形：节点 0 后 `pos = 1`，节点 1 的 `FIND_FWD "."` 在剩余范围内查不到 → 第 1 个节点失败，返回 `-2`（`stride_match_run` 对外压成 `-1`）。

`"x2.0"` 的情形：节点 0 的 `COMPARE "v"` 在比特偏移 0 处不等 → 返回 `-1`。

### 8.2 提取：手工构建 `${4}$'-'${2}$'-'${2}` 并提取 `"2024-03-15"`

```c
#include <stdio.h>
#include <string.h>

#include "stride/stride.h"

int main(void) {
    stride_seq_t *e = stride_seq_new();

    stride_seq_capture_steps(e, 4);          /* ${4}   捕获 4 步（4 字节 = 32 比特）*/
    stride_seq_skip_bits(e, STRIDE_BITS(1)); /* $'-'   跳过 1 字节的定界字符 */
    stride_seq_capture_steps(e, 2);          /* ${2}   捕获 2 步（16 比特）*/
    stride_seq_skip_bits(e, STRIDE_BITS(1)); /* $'-'   跳过 1 字节 */
    stride_seq_capture_steps(e, 2);          /* ${2}   捕获 2 步（16 比特）*/

    const char *date = "2024-03-15";
    stride_param_t p[8];
    size_t n = 0;

    printf("nodes = %zu, params = %zu\n",
           stride_seq_count(e), stride_seq_param_count(e)); /* nodes = 3, params = 3 */

    if (stride_extract_run(e, 8, date, STRIDE_BITS(strlen(date)), p, 8, &n) == 0) {
        for (size_t i = 0; i < n; i++) {
            printf("p[%zu] = %.*s (%zu bits)\n", i,
                   (int)(p[i].bit_len / 8), (const char *)p[i].ptr, p[i].bit_len);
        }
    } else {
        printf("extract failed\n");
    }
    /* 输出：
       p[0] = 2024 (32 bits)
       p[1] = 03   (16 bits)
       p[2] = 15   (16 bits)  */

    stride_seq_free(e);
    return 0;
}
```

构建出的链表（`count == 3`、`param_count == 3`）：

| 节点 | `move` | `move_value` | `act` | `act_value` | 来源 |
|------|--------|-------------|-------|------------|------|
| 0 | `STRIDE_MOVE_NONE` | 0 | `STRIDE_ACT_CAPTURE_STEPS` | 4 | `${4}` |
| 1 | `STRIDE_MOVE_SKIP_BITS` | 8 | `STRIDE_ACT_CAPTURE_STEPS` | 2 | `$'-'` + `${2}` |
| 2 | `STRIDE_MOVE_SKIP_BITS` | 8 | `STRIDE_ACT_CAPTURE_STEPS` | 2 | `$'-'` + `${2}` |

节点 1、2 的形态值得注意：**跳过定界字符并不单独占节点**，而是与随后的捕获动作共享一个节点。逐调用看：

1. `capture_steps(4)` 在空序列上新建节点 0：`{NONE, CAPTURE_STEPS 4}`；
2. `skip_bits(8)` 发现尾节点（节点 0）**已有动作**（`CAPTURE_STEPS`），不能把偏移叠加上去，于是新建节点 1：`{SKIP_BITS 8, NONE}`；
3. `capture_steps(2)` 发现尾节点（节点 1）尚无动作，**绑定**到它身上：节点 1 变成 `{SKIP_BITS 8, CAPTURE_STEPS 2}`；
4. `skip_bits(8)` 又发现尾节点已有动作，新建节点 2：`{SKIP_BITS 8, NONE}`；
5. `capture_steps(2)` 绑定到节点 2：`{SKIP_BITS 8, CAPTURE_STEPS 2}`。

关键点在第 2、4 步与第 3、5 步的**配合**：跳过定界字符本身确实需要一个新节点（尾节点已有动作），但紧随其后的捕获会绑定到这个新节点上，于是二者共享一个节点，`count` 最终是 3 而不是 5。反过来，若 `skip_bits` 之后**没有**捕获动作接上，它就会成为一个只含偏移的独立节点——例如在该序列末尾再加一次 `skip_bits(8)`，会得到第 4 个节点 `{SKIP_BITS 8, NONE}`。**是否独占节点取决于相邻调用，而不是 `SKIP_BITS` 本身。**

执行 `"2024-03-15"`（10 字节，`total = 10`）的过程：

| 节点 | 偏移 | 动作 | 执行后 `pos` |
|------|------|------|-------------|
| 0 | `NONE` | 捕获步 0..3 → `{ptr="2024", 32 比特}` | 4 |
| 1 | `SKIP_BITS 8` → `8 / 8 = 1` 步 | 捕获步 5..6 → `{ptr="03", 16 比特}` | 7 |
| 2 | `SKIP_BITS 8` → 1 步 | 捕获步 8..9 → `{ptr="15", 16 比特}` | 10 |

`pos == total == 10`，段尾对齐；`stride_extract_run()` 返回 `0`，`n == 3`。三个参数的 `ptr` 都直接指向 `date` 字符串内部（零拷贝），起始比特偏移分别为 0、5×8=40、8×8=64，均为 8 的整数倍。

若把 `param_capacity` 传成 `2`，第 3 个捕获动作会因 `out_idx >= param_capacity` 失败，`stride_extract_run()` 返回 `-1`。

### 8.3 多段提取

```c
#include <string.h>

#include "stride/stride.h"

void extract_two_segments(void) {
    stride_seq_t *s0 = stride_seq_new();
    stride_seq_capture_steps(s0, 2);   /* 第 0 段：前 2 步 */

    stride_seq_t *s1 = stride_seq_new();
    stride_seq_capture_end(s1);        /* 第 1 段：直到段尾 */

    stride_extractor_t *arr[2] = {s0, s1};
    stride_full_extractor_t *full = stride_full_extractor_create(arr, 2);
    if (!full) {
        return;
    }
    /* full->total_params == 2 */

    const void *segs[2] = {"ab", "cde"};
    size_t lens[2] = {STRIDE_BITS(2), STRIDE_BITS(3)};

    stride_param_t p[8];
    size_t n = 0;
    if (stride_full_extractor_run(full, 8, segs, lens, 2, p, 8, &n) == 0) {
        /* n == 2；p[0] = "ab"（来自第 0 段），p[1] = "cde"（来自第 1 段），
           参数按段顺序连接 */
    }

    stride_full_extractor_destroy(full); /* 同时释放 s0 与 s1 */
}
```

---

## 九、边界与限制

以下限制**属于模型本身**，不是实现缺陷；它们直接来自「步长 + 比特串」这两个原语，无法通过 API 调整消除。

### 9.1 变长编码下「恰好 N 个字符」无法表达

`CAPTURE_STEPS` 捕获的是 `N` **步**，而不是 `N` 个字符。在 UTF-8 这类变长编码下，一个字符占 1~4 字节，因此：

- 无法用 `stride_seq_capture_steps(seq, N)` 表达「恰好 N 个字符」；
- 若强行用「N 字节」近似，遇到多字节字符就会切到字符中间或提前结束。

要按字符边界截取，只能由**知道编码的调用方**处理：要么用 `CAPTURE_UNTIL` 以定界比特串为界，要么在执行后按返回的比特串自行判断字符边界。Stride 自身不解释编码，这是它与「正则表达式引擎」在能力边界上的根本分野。

### 9.2 非自同步编码可能匹配到字符内部

查找（`FIND_FWD` / `FIND_REV`）与 `CAPTURE_UNTIL` 都在**步对齐**的格点上滑动比特窗口做逐比特比较，只保证落点是步的整数倍——**不保证**落点是字符边界。

因此在一个非自同步的编码里（例如某些多字节编码中，某个字节序列恰好是另一个字符的尾部子串），查找可能命中「字符内部」的位置。对 UTF-8 而言，由于它是自同步编码且 Stride 从步对齐位置起比较完整字节，实际风险很低；但对任意二进制或非自同步编码，**这是一个真实的语义边界**：

- Stride 的查找是**比特/字节层面的首次出现**，不是「词」或「字符」层面的出现；
- 需要字符级语义时，必须由调用方在结果上做二次确认。

同理，`COMPARE` 只是「这一段比特相等」，不蕴含任何编码层面的合法性。

### 9.3 非字节对齐捕获无法用 (ptr, bit_len) 表达

`stride_param_t` 是 `(const void *ptr, size_t bit_len)`：

- `ptr` 只能指向**字节**；
- `bit_len` 可以不是 8 的倍数（例如步长 1 下捕获 3 比特、`bit_len == 3`），但 `ptr` 无法表达「从某字节的第 3 比特开始」。

所以引擎要求捕获**起点**字节对齐（`pos × stride % 8 == 0`），否则**直接失败**。这不是「不支持非字节对齐的捕获」，而是「无法用当前参数表示法无歧义地表达它」——与其静默地移位打包出一个语义可疑的缓冲区，不如显式失败。

若确实需要非字节对齐的比特串，调用方只能自己在段上按比特偏移访问。

### 9.4 其他约定带来的边界

| 约束 | 说明 |
|------|------|
| 段长必须是步长整数倍 | 否则执行立即失败（`-1`）；段尾的「零头」无法参与序列 |
| `COMPARE` 消耗步数必须整除 | `act_target.bit_len % stride != 0` 时该节点失败 |
| 正向查找目标长度必须整除步长 | `FIND_FWD` / `CAPTURE_UNTIL` 的目标若长度不是步长整数倍则查不到 |
| 参数容量必须足够 | `out_idx >= param_capacity` 时捕获失败；`stride_seq_param_count()` 可用于预估 |
| 拷贝是整字节的 | 非 8 倍数长度的比特串，其末尾字节的高位噪声会被一并复制，但不参与比对 |
| 构建期无对齐校验 | 构建只检查 `seq` 非空与内存分配；位置可达性、对齐都在执行期判定 |
| 失败码信息量 | `stride_match_run()` / `stride_extract_run()` 只返回 `0`/`-1`；需要节点级定位请直接用 `stride_seq_run()` |

---

## 十、API 索引

| 头文件 | 内容 |
|--------|------|
| `stride/types.h` | `stride_blob_t`、`stride_param_t`、`stride_move_t`、`stride_act_t`、`stride_step_t`、`stride_seq_t`、`stride_status_t`、`STRIDE_BITS` |
| `stride/sequence.h` | `stride_seq_new/free/clear/count/param_count`、7 个偏移构建函数、4 个动作构建函数、`stride_seq_run` |
| `stride/matcher.h` | `stride_match_run` |
| `stride/extractor.h` | `stride_extractor_t`、`stride_extract_run`、`stride_full_extractor_t` 及 `create/destroy/run` |
| `stride/stride.h` | 汇总包含上述全部头文件，并定义 `STRIDE_VERSION_*` |

---

**文档版本**：1.0
**更新日期**：2026-09-11
