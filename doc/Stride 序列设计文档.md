# Stride 序列设计文档

**文档版本**：1.2
**更新日期**：2026-09-15
**适用模块**：`stride/types.h`、`stride/sequence.h`、`stride/matcher.h`、`stride/extractor.h`
**适用版本**：Stride 3.0.0

---

> **阅读须知**：本文档描述 2026-09-11 重构后的 **v3 架构**。v2 的「操作符序列 → 序列（数组）+ 状态机」编译模型与相应数据结构、API 名称均已废弃（v3 已废弃状态机），本文档不沿用它们。v3 中序列是**单链表**、构建是**函数式**的。
>
> **范围**：Stride **不含语法，也不含编译器**。把 `$'…'`、`${…}`、`$[…]` 这类模式翻译成 Stride 的构建函数调用，是调用方（例如 URLRouter）的职责。Stride 只提供「函数式序列构建 + 通用执行引擎」。
>
> **2026-09-15 简化**：移除步长概念，将最小操作单位固定为**1 字节**。所有 API 的段长、偏移、长度参数均以**字节**为单位。

---

## 一、概述

### 1.1 定位

Stride 是一个用 C99 编写的**步进式字节串匹配 / 提取库**，零外部依赖。它把输入看作**不透明二进制**：既不知道输入是文本还是二进制，也不解释任何编码，更不关心调用方用什么分隔符切分输入。

### 1.2 原语

整个库只建立在一个原语之上：

| 原语 | 回答的问题 | 定义 |
|------|-----------|------|
| **字节串 blob** | 比什么 | `stride_blob_t { const void *data; size_t len; }`，`len` 为**字节长度** |

由此，同一套机制可以覆盖多种输入形态：

| 输入形态 | 说明 |
|---------|------|
| 字节流（UTF-8、任意二进制） | 最常见；一字节一字节地比对 |
| 任意二进制 | 不解释编码，纯比特比对 |

### 1.3 与分隔符、编码无关

Stride 的**执行引擎不解释任何编码**，也不认识任何分隔符：

- **不切分输入**：把输入按 `/` 等分隔符切成「段（segment）」是调用方的职责；Stride 的每次执行只面对**一个段**（`stride_extract_run` / `stride_seq_run` 都是单段入口，多段由 `stride_full_extractor_t` 顺序组合）。
- **不识别字符**：所谓「比对字面量」就是比对一段字节串；`$'v'` 与 `$'中文'` 在 Stride 眼里没有区别，前者 1 字节、后者 3 字节（UTF-8）。
- **不理解变长编码**：UTF-8 的一个字符占几个字节，Stride 不知道也不判断；它只会按字节走、按字节比。
- **查找是字节查找**：`FIND_*` 在字节格点上滑动做逐字节比较，与「词」或「字符」的概念无关。

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

| 量 | 出现位置 | 单位 | 说明 |
|----|---------|------|------|
| **段长** `segment_len` | 各执行入口 | **字节** | 输入段的字节长度 |
| **字面量长度** `len` | `stride_blob_t.len`、`literals` 参数 | **字节** | 字节长度 |
| **位置 / 偏移** `bytes` | `stride_seq_step_fwd/back()`、`abs_head/end()`、`move_value` | **字节** | 字节偏移 |
| **捕获长度** `act_value` | `stride_seq_capture_bytes()` | **字节** | 捕获 `act_value` 字节 |
| **参数长度** `len` | `stride_param_t.len` | **字节** | 参数的字节长度 |
| **查找目标长度** | `stride_seq_find_fwd/rev()` 的 `target.len` | **字节** | 字节长度 |

### 2.2 段内位置模型

段有不透明二进制内容 `segment` 与字节长度 `segment_len`。

| 概念 | 定义 |
|------|------|
| 段长（字节） | `total = segment_len` |
| 游标 `pos` | 当前所在的**字节序号**，初值 `0`（段首） |
| **HEAD** | 段首，即第 `0` 字节；不变基准 |
| **END** | 段尾，即第 `total` 字节；随段长变化 |

`total` 直接由 `segment_len` 给出。

### 2.3 对齐约束

| 约束 | 位置 | 违反后果 |
|------|------|---------|
| 捕获起点必须**字节对齐**：`pos % 1 == 0` | 全部捕获动作 | 该节点失败 |

由于最小单位就是 1 字节，因此捕获起点天然字节对齐，不再需要额外的对齐检查。

---

## 三、数据结构

### 3.1 字节串 `stride_blob_t`

```c
typedef struct {
    const void *data;
    size_t len;
} stride_blob_t;
```

| 字段 | 含义 |
|------|------|
| `data` | 指向原始数据首字节 |
| `len` | **字节长度** |

`stride_blob_t` **本身不表达所有权**：它只是一个「指针 + 长度」的视图。所有权取决于它出现在哪里：

| 场合 | 所有权 |
|------|--------|
| 作为构建函数入参（`literal` / `target`） | 调用方拥有；构建时 Stride 会**复制**一份副本到节点里 |
| 作为节点字段 `move_target` / `act_target` | **节点拥有**，由 `stride_seq_free()` / `stride_seq_clear()` 释放 |
| 作为 `stride_param_t` 返回 | **不拥有**，零拷贝指向输入段内部 |

复制规则（`blob_dup`）：若 `src` 为 `NULL`、`src->data` 为 `NULL` 或 `src->len == 0`，则节点内为空 blob（`data = NULL`、`len = 0`）；否则申请 `len` 字节并 `memcpy`。

### 3.2 参数 `stride_param_t`

```c
typedef struct {
    const void *ptr;
    size_t len;
} stride_param_t;
```

零拷贝的「指针 + **字节长度**」对，直接指向**输入段内部**；`ptr` 指向包含该起始字节的第一个字节。

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
| `move_value` | `size_t` | 偏移量。**单位为字节**（`STEP_FWD` / `STEP_BACK` / `ABS_HEAD` / `ABS_END`）；`FIND_*` 与 `NONE` 时未使用（构建函数一律写 `0`） |
| `move_target` | `stride_blob_t` | `FIND_FWD` / `FIND_REV` 的查找目标；**由本节点拥有**，`stride_seq_free()` 释放；其他 move 类型为空 blob |
| `act` | `stride_act_t` | 到位后要执行的动作；`STRIDE_ACT_NONE` 表示只移动、不做动作 |
| `act_target` | `stride_blob_t` | `COMPARE` 要比对的字面量、`CAPTURE_UNTIL` 的终止字节串；**由本节点拥有**；其余 act 类型为空 blob |
| `act_value` | `size_t` | **单位为字节**，仅 `CAPTURE_BYTES` 使用（捕获 `act_value` 字节）；其余 act 类型未使用（构建函数写 `0`） |
| `next` | `struct stride_step *` | 单链表后继；尾节点为 `NULL` |

### 3.4 偏移枚举 `stride_move_t`

```c
typedef enum {
    STRIDE_MOVE_NONE = 0,
    STRIDE_MOVE_STEP_FWD,
    STRIDE_MOVE_STEP_BACK,
    STRIDE_MOVE_ABS_HEAD,
    STRIDE_MOVE_ABS_END,
    STRIDE_MOVE_FIND_FWD,
    STRIDE_MOVE_FIND_REV
} stride_move_t;
```

| 枚举值 | `move_value` 单位 | 语义 | 失败条件（执行期） |
|--------|------------------|------|------------------|
| `STRIDE_MOVE_NONE` | — | 不移动，游标保持 | 从不失败 |
| `STRIDE_MOVE_STEP_FWD` | **字节** | 向段尾走 `move_value` 字节 | `move_value > total - pos`（越出段尾） |
| `STRIDE_MOVE_STEP_BACK` | **字节** | 向段首走 `move_value` 字节 | `move_value > pos`（越出段首） |
| `STRIDE_MOVE_ABS_HEAD` | **字节** | 定位到第 `move_value` 字节（`HEAD + n`，绝对位置） | `move_value > total` |
| `STRIDE_MOVE_ABS_END` | **字节** | 定位到 `END − move_value` 字节（`move_value == 0` 即段尾） | `move_value > total` |
| `STRIDE_MOVE_FIND_FWD` | —（用 `move_target`） | 从当前位置起**向段尾**查找 `move_target`，落点为目标首字节 | 目标为空/查不到 |
| `STRIDE_MOVE_FIND_REV` | —（用 `move_target`） | 从当前位置起**向段首**查找 `move_target`，落点为目标首字节 | 同上 |

查找的落点语义（`seg_find_fwd` / `seg_find_rev`）：查找是**字节对齐**的——窗口只在字节的整数倍格点上滑动，落点 `hit` 记录目标首字节，随后 `pos = hit`。`FIND_FWD` 从 `pos` 起向尾部扫描；`FIND_REV` 从 `pos - 1`（`pos == 0` 时从 `total - 1`）起向头部扫描。

### 3.5 动作枚举 `stride_act_t`

```c
typedef enum {
    STRIDE_ACT_NONE = 0,
    STRIDE_ACT_COMPARE,
    STRIDE_ACT_CAPTURE_BYTES,
    STRIDE_ACT_CAPTURE_UNTIL,
    STRIDE_ACT_CAPTURE_END
} stride_act_t;
```

| 枚举值 | 使用字段 | 语义 | 失败条件（执行期） |
|--------|---------|------|------------------|
| `STRIDE_ACT_NONE` | — | 到位后不做任何事 | 从不失败 |
| `STRIDE_ACT_COMPARE` | `act_target` | 在游标处比对 `act_target`，成功后游标前进 `act_target.len` 字节 | 越出段尾、字节不等 |
| `STRIDE_ACT_CAPTURE_BYTES` | `act_value` | 捕获 `act_value` **字节**并前移 | `params == NULL`、越出段尾、参数容量不足 |
| `STRIDE_ACT_CAPTURE_UNTIL` | `act_target` | 捕获从游标到 `act_target` **首次出现位置之前**的字节；未找到则捕获到段尾。游标移到 `act_target` 首字节或段尾 | `params == NULL`、参数容量不足 |
| `STRIDE_ACT_CAPTURE_END` | — | 捕获从游标到**段尾**的全部剩余字节，游标移到段尾 | `params == NULL`、参数容量不足 |

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
    STRIDE_E_NOMEM
} stride_status_t;
```

| 枚举值 | 字符串（`stride_status_str`） | 含义 |
|--------|------------------------------|------|
| `STRIDE_OK` | `"ok"` | 成功 |
| `STRIDE_E_INVALID_PATTERN` | `"invalid pattern"` | 模式格式无效 |
| `STRIDE_E_EMPTY_SEGMENT` | `"empty segment"` | 空的段模式 |
| `STRIDE_E_NOMEM` | `"out of memory"` | 内存分配失败 |

该枚举用于**调用方自身的模式处理流程**（例如 URLRouter 在把模式翻译成构建调用时报告错误）。Stride 的序列构建与执行 API 一律使用 `0` / `-1` 与负数失败码，见第五、六节。

---

## 四、单链表与尾部合并

### 4.1 构建是函数式追加

序列的构建**没有状态机**：每一次调用只做一件事——尝试把新的偏移或动作**追加到尾节点**，能就地合并就合并，否则新建尾节点。

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

| 已有 \ 新增 | `STEP_FWD` | `STEP_BACK` | `FIND_FWD/REV` | `ABS_HEAD` / `ABS_END` / `NONE` |
|---|---|---|---|---|
| **`STEP_FWD`** | ✅ **相加**：`n += v` | ✅ **抵消**：`v ≤ n` 时 `n -= v`；否则**翻转**为 `STEP_BACK`，值 `v - n` | ❌ 不合并（`FIND_*` 从不合并） | — |
| **`STEP_BACK`** | ✅ **抵消**：`v ≥ n` 时**翻转**为 `STEP_FWD`，值 `v - n`；否则 `n -= v` | ✅ **相加**：`n += v` | ❌ 不合并 | — |
| **`ABS_HEAD`** | ✅ **相加**：`n += v` | ✅ 仅当 `v ≤ n` 时**相加（相减）**：`n -= v`；否则 ❌ 不合并 | ❌ 不合并 | — |
| **`ABS_END`** | ❌ 不合并 | ✅ **相加**：`n += v` | ❌ 不合并 | — |
| **`FIND_FWD` / `FIND_REV`** | ❌ 从不合并 | ❌ 从不合并 | ❌ 从不合并 | — |
| **`NONE`**（新节点默认值） | ❌ `move_try_merge` 不处理 | ❌ | ❌ | — |

> 表中 `n` 是尾节点已有的 `move_value`，`v` 是本次新增的值。`ABS_HEAD` / `ABS_END` / `NONE` 永远不会作为**新增**类型出现（构建函数只产生前五种偏移），故该列无意义。
>
> `move_target` 非空（即 `FIND_*`）时 `seq_add_move` 在第 2 步就放弃合并；即使侥幸走到 `move_try_merge`，其 `default` 分支也只返回「不可合并」。因此「`FIND_*` 从不合并」有两重保证。
>
> 尾节点 `move == NONE` 时本表所有「不合并」结论都成立——`move_try_merge` 的 `switch` 落到 `default`，返回 `0`，由调用方新建节点。

**为什么要合并**：连续步进是最常见的模式（`$[>3]$'x'`、`${4}$'-'` 等），把它们折叠成一个节点，能显著减少节点数和执行时的分支次数，同时让链表长度反映「真正的位置决策点数量」。

**为什么有些组合不合并**：

| 组合 | 原因 |
|------|------|
| `ABS_HEAD` + `STEP_BACK`（`v > n`） | 结果 `HEAD + n − v` 可能落在段首之前。`ABS_HEAD` 的 `move_value` 是无符号的「段首偏移」，无法表示为负；保持两个节点，让执行期按「先定位到 HEAD+n，再后退 v 字节」判定失败，语义更直观 |
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

构建期**不做**段长、位置可达性检查——这些只有执行期才知道（段长是执行期参数）。构建函数只在两类情况下返回 `-1`：

- `seq == NULL`；
- 内存分配失败（新建节点或复制字节串时 `malloc` 失败）。

任何「能构建出来但执行时必然失败」的序列（例如连续三步 `STEP_BACK`、或 `ABS_HEAD 100` 而段只有 3 字节）都是合法的构建结果。

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
| `stride_seq_free` | `seq` | 释放全部节点、节点内两个字节串副本，再释放序列本身。`seq == NULL` 时无操作 | 无 |
| `stride_seq_clear` | `seq` | 释放全部节点与字节串副本，并把容器**归零复用**（`count = param_count = 0`，`head = tail = NULL`）。`seq == NULL` 时无操作 | 无 |
| `stride_seq_count` | `seq` | 节点数 | `seq == NULL` 时返回 `0` |
| `stride_seq_param_count` | `seq` | 构建期累计的**捕获动作个数** | `seq == NULL` 时返回 `0` |

`stride_seq_clear` 保留了 `stride_seq_t` 这块容器本身，适合「一次分配、反复重建」的使用方式；但**不会**回收已释放节点占用的堆内存。

### 5.2 偏移构建函数

```c
int stride_seq_step_fwd(stride_seq_t *seq, size_t bytes);
int stride_seq_step_back(stride_seq_t *seq, size_t bytes);
int stride_seq_abs_head(stride_seq_t *seq, size_t bytes);
int stride_seq_abs_end(stride_seq_t *seq, size_t bytes);
int stride_seq_find_fwd(stride_seq_t *seq, const stride_blob_t *target);
int stride_seq_find_rev(stride_seq_t *seq, const stride_blob_t *target);
```

| 函数 | 参数 | 单位 | 语义 | 返回 |
|------|------|------|------|------|
| `stride_seq_step_fwd` | `bytes` | 字节 | 向段尾走 `bytes` 字节（`STRIDE_MOVE_STEP_FWD`） | `0` 成功 / `-1` 失败 |
| `stride_seq_step_back` | `bytes` | 字节 | 向段首走 `bytes` 字节（`STRIDE_MOVE_STEP_BACK`） | `0` / `-1` |
| `stride_seq_abs_head` | `bytes` | 字节 | 定位到第 `bytes` 字节（`HEAD + bytes`） | `0` / `-1` |
| `stride_seq_abs_end` | `bytes` | 字节 | 定位到 `END − bytes` 字节；`0` 即段尾（常用于 `${}`） | `0` / `-1` |
| `stride_seq_find_fwd` | `target` | — | 从当前位置向段尾查找 `target` 字节串，落点为其首字节 | `0` / `-1` |
| `stride_seq_find_rev` | `target` | — | 从当前位置向段首查找 `target` 字节串，落点为其首字节 | `0` / `-1` |

`target` 会被**复制**进节点（`blob_dup`），调用后调用方即可释放自己的数据；`target->len` 应为其**字节长度**。

`stride_seq_find_fwd` 与 `stride_seq_find_rev` 的内部 `move_value` 一律写 `0`，查找目标只放在 `move_target` 中。

### 5.3 动作构建函数

```c
int stride_seq_compare(stride_seq_t *seq, const stride_blob_t *literal);
int stride_seq_capture_bytes(stride_seq_t *seq, size_t bytes);
int stride_seq_capture_until(stride_seq_t *seq, const stride_blob_t *target);
int stride_seq_capture_end(stride_seq_t *seq);
```

| 函数 | 参数 | 单位 | 语义 | `param_count` | 返回 |
|------|------|------|------|--------------|------|
| `stride_seq_compare` | `literal` | — | 在游标处比对 `literal` 字节串并前进其字节数 | 不变 | `0` / `-1` |
| `stride_seq_capture_bytes` | `bytes` | 字节 | 捕获 `bytes` 字节 | **+1** | `0` / `-1` |
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
| `stride_seq_step_fwd` | 偏移 | `bytes`（字节） | `move = FWD`，`move_value` |
| `stride_seq_step_back` | 偏移 | `bytes`（字节） | `move = BACK`，`move_value` |
| `stride_seq_abs_head` | 偏移 | `bytes`（字节） | `move = ABS_HEAD`，`move_value` |
| `stride_seq_abs_end` | 偏移 | `bytes`（字节） | `move = ABS_END`，`move_value` |
| `stride_seq_find_fwd` | 偏移 | `target`（字节串） | `move = FIND_FWD`，`move_target` |
| `stride_seq_find_rev` | 偏移 | `target`（字节串） | `move = FIND_REV`，`move_target` |
| `stride_seq_compare` | 动作 | `literal`（字节串） | `act = COMPARE`，`act_target` |
| `stride_seq_capture_bytes` | 动作 | `bytes`（字节） | `act = CAPTURE_BYTES`，`act_value` |
| `stride_seq_capture_until` | 动作 | `target`（字节串） | `act = CAPTURE_UNTIL`，`act_target` |
| `stride_seq_capture_end` | 动作 | — | `act = CAPTURE_END` |

---

## 六、通用执行引擎 `stride_seq_run()`

### 6.1 原型与参数

```c
int stride_seq_run(const stride_seq_t *seq, const void *segment,
                   size_t segment_len, stride_param_t *params,
                   size_t param_capacity, size_t *param_count);
```

| 参数 | 入/出 | 语义 |
|------|------|------|
| `seq` | 入 | 步进序列；`NULL` → 返回 `-1` |
| `segment` | 入 | 段数据（不透明二进制）；`NULL` → 返回 `-1` |
| `segment_len` | 入 | 段**总字节数** |
| `params` | 出 | 参数缓冲；**`NULL` 表示纯匹配**（遇到捕获动作即失败） |
| `param_capacity` | 入 | `params` 容量（可写参数个数上限） |
| `param_count` | 入/出 | 入参：已写入参数个数（起始下标）；出参：执行后的总数。可为 `NULL`（此时按 `0` 起算且不回写） |

### 6.2 执行流程

```
        ┌──────────────────────────────────────────────┐
        │ 前置检查                                      │
        │  seq / segment 非空                            │
        └───────────────────────┬──────────────────────┘
                                ▼
        total = segment_len ; pos = 0 ; i = 0
                                │
        ┌───────────────────────▼──────────────────────┐
        │ 对每个节点 n（i = 0,1,…,count-1）：            │
        │   ① 执行偏移  →  更新 pos                     │
        │   ② 执行动作  →  比对 / 写 params / 移动 pos   │
        │   失败 → return -(i+1)                        │
        └───────────────────────┬──────────────────────┘
                                ▼
        pos != total  → return -(count+1)             （段尾未对齐）
                                ▼
        *param_count = 写入后的总数 ;  return 0
```

### 6.3 前置检查

| 检查 | 失败返回 |
|------|---------|
| `seq == NULL` 或 `segment == NULL` | `-1` |

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
| `FIND_FWD` | 从 `pos` 向段尾找 `move_target`，`pos = hit` | 查不到（含目标为空） |
| `FIND_REV` | 从 `pos - 1`（`pos == 0` 时从 `total - 1`）向段首找，`pos = hit` | 查不到；段长为 0 |

**动作阶段**（按 `act` 分支）：

| `act` | 执行 | 失败条件 |
|-------|------|---------|
| `NONE` | 无操作 | 无 |
| `COMPARE` | 在字节偏移 `pos` 处逐字节（`memcmp`）比对 `act_target`；`pos += act_target.len` | `act_target.len > total - pos`；字节不相等 |
| `CAPTURE_BYTES` | `params[out_idx] = { seg + pos, act_value }`；`out_idx++`；`pos += act_value` | `params == NULL`；`act_value > total - pos`；`out_idx >= param_capacity` |
| `CAPTURE_UNTIL` | 从 `pos` 起查找 `act_target`，`end = hit`（未找到则 `end = total`）；写 `{ seg + pos, end − pos }`；`pos = end` | `params == NULL`；`out_idx >= param_capacity` |
| `CAPTURE_END` | 写 `{ seg + pos, total − pos }`；`pos = total` | `params == NULL`；`out_idx >= param_capacity` |

其中 `out_idx` 的初值是 `param_count ? *param_count : 0`，因此 `param_count` 可用于在**同一次执行内跨序列**把参数连续追加到同一缓冲（多段提取正是靠这一点，见 7.4）。

### 6.5 比对实现

`COMPARE` 走 `memcmp` 逐字节比较：

- 若 `act_target.len == 0` 视为相等（空字面量恒真）。

查找（`FIND_*`）与 `CAPTURE_UNTIL` 内部都复用 `memcmp`，因此它们的比较语义与 `COMPARE` 完全一致。

### 6.6 段尾对齐要求

**无论匹配还是提取**，执行结束（所有节点走完）时都要求：

```c
pos == total        /* 游标恰好落在段尾 */
```

否则返回 `-(count + 1)`。

这条要求是「整段模式」的语义体现：序列描述的是**整个段**的形态，而不是段内某个子串。因此：

- 只走了一部分就结束 → 失败；
- 走过头（越出段尾）→ 更早在偏移阶段就失败（`-(i+1)`），而不会等到段尾检查。

「纯偏移节点」因此也有实际意义：`stride_seq_step_fwd(m, 3)` 单独执行时，成功**当且仅当**段恰好是 3 字节——它同时充当字节数与段长的校验。

### 6.7 返回值约定

| 返回值 | 含义 |
|--------|------|
| `0` | 成功（且 `pos == total`） |
| `-(i + 1)` | 第 `i` 个节点失败（`i` 从 `0` 起），即 `-1` 表示第 0 个节点失败、`-2` 表示第 1 个…… |
| `-(count + 1)` | 全部节点成功但**段尾未对齐**（`pos != total`） |
| `-1`（前置检查） | `seq == NULL` 或 `segment == NULL` |

`stride_match_run()` 与 `stride_extract_run()` 会把任何负值统一压成 `-1`；需要定位失败节点时应直接调用 `stride_seq_run()`。

### 6.8 `params == NULL` 表示纯匹配

当 `params == NULL` 时：

- 所有 `CAPTURE_*` 动作**立即失败**（返回该节点的 `-(i+1)`）；
- `COMPARE` 与偏移不受影响。

这正是 `stride_match_run()` 的实现方式：以「空参数缓冲」执行序列。于是**同一个序列**既可以当匹配序列用（只关心是否走通），也可以当提取序列用（同时收参数）——只要它不含捕获动作。

`param_count` 也可以为 `NULL`：此时起始下标按 `0` 计算，执行后不回写总数。

---

## 七、匹配用法与提取用法

### 7.1 匹配：`stride_match_run()`

```c
int stride_match_run(const stride_seq_t *seq, const void *segment, size_t segment_len);
```

| 参数 | 语义 |
|------|------|
| `seq` | 匹配序列，**只应含偏移与 `STRIDE_ACT_COMPARE`** |
| `segment` | 段数据 |
| `segment_len` | 段总字节数 |

**返回**：`0` 匹配成功；负数表示失败（`-1` 表示一般失败或第 0 个节点失败，其余负值表示第 `|r|-1` 个节点失败，或段尾未对齐）。

实现就是一次「不带参数缓冲」的通用执行：

```c
return stride_seq_run(seq, segment, segment_len, NULL, 0, NULL);
```

匹配序列的两种构造方式：

| 希望表达 | 构造方式 |
|---------|---------|
| 在当前位置比对字面量 | `stride_seq_compare(seq, &lit)` |
| 先走 `n` 字节再比对 | `stride_seq_step_fwd(seq, n)` → `stride_seq_compare(seq, &lit)`（绑定到同一节点） |

### 7.2 提取：`stride_extract_run()`

```c
int stride_extract_run(const stride_extractor_t *ex, const void *segment,
                       size_t segment_len, stride_param_t *params,
                       size_t param_capacity, size_t *param_count);
```

| 参数 | 语义 |
|------|------|
| `ex` | 提取序列，**只应含偏移与捕获动作** |
| `segment` | 段数据 |
| `segment_len` | 段总字节数 |
| `params` | 参数缓冲（调用方分配） |
| `param_capacity` | `params` 容量 |
| `param_count` | 入参：已写入参数个数；出参：执行后的总数 |

**返回**：`0` 提取成功；负数表示失败。

实现就是一次「带参数缓冲」的通用执行：

```c
return stride_seq_run(ex, segment, segment_len, params, param_capacity, param_count) == 0 ? 0 : -1;
```

提取序列的构造方式：

| 希望表达 | 构造方式 |
|---------|---------|
| 捕获 `n` 字节 | `stride_seq_capture_bytes(seq, n)` |
| 捕获到定界串之前 | `stride_seq_capture_until(seq, &delim)` |
| 捕获到段尾 | `stride_seq_capture_end(seq)` |
| 跳过已验证字面量 | `stride_seq_find_fwd(seq, &lit)`（提取阶段用查找跳过） |

### 7.3 多段提取：`stride_full_extractor_run()`

```c
int stride_full_extractor_run(const stride_full_extractor_t *full,
                              const void *const *segments,
                              const size_t *seg_lens, size_t segment_count,
                              stride_param_t *params, size_t param_capacity,
                              size_t *out_count);
```

| 参数 | 语义 |
|------|------|
| `full` | 完整提取器（包含多个段的提取序列） |
| `segments` | 段数组 |
| `seg_lens` | 每段字节长度数组 |
| `segment_count` | 段数 |
| `params` | 参数缓冲 |
| `param_capacity` | 参数缓冲容量 |
| `out_count` | 输出参数总数 |

**返回**：`0` 提取成功；负数表示失败。

多段提取按段顺序执行，参数按顺序追加到同一缓冲。

---

## 八、实例

### 8.1 匹配版本号：$'v'${'.'}$'.'${}

```c
stride_seq_t *m = stride_seq_new();
stride_blob_t v = blob("v");
stride_blob_t dot = blob(".");

stride_seq_compare(m, &v);    /* $'v' */
stride_seq_find_fwd(m, &dot); /* ${'.'} 匹配阶段 = 查找 */
stride_seq_compare(m, &dot);  /* $'.' 绑到上一个节点 */
stride_seq_abs_end(m, 0);     /* ${} */
```

节点形态：

```
[0] move=FIND_FWD "."  act=COMPARE "."
[1] move=ABS_END 0     act=NONE
```

执行 `stride_match_run(m, "v2.0", 4)`：

1. 节点 0：查找 `.` 找到位置 1，比对 `.` 成功，游标移到 2
2. 节点 1：定位到段尾，游标已在 4，成功

### 8.2 提取日期：${4}$'-'${2}$'-'${2}

```c
stride_seq_t *e = stride_seq_new();
stride_seq_capture_bytes(e, 4);          /* ${4} */
stride_seq_find_fwd(e, &blob("-"));      /* $'-' */
stride_seq_capture_bytes(e, 2);          /* ${2} */
stride_seq_find_fwd(e, &blob("-"));      /* $'-' */
stride_seq_capture_bytes(e, 2);          /* ${2} */
```

节点形态：

```
[0] move=NONE         act=CAPTURE_BYTES 4
[1] move=FIND_FWD "-" act=NONE
[2] move=NONE         act=CAPTURE_BYTES 2
[3] move=FIND_FWD "-" act=NONE
[4] move=NONE         act=CAPTURE_BYTES 2
```

执行 `stride_extract_run(e, "2024-03-15", 10, ...)` 返回三个参数：`"2024"`、`"03"`、`"15"`。

---

## 九、边界与限制

### 9.1 不支持的操作

| 操作 | 原因 |
|------|------|
| 比特级操作（步长 1、4 等非字节单位） | 已移除，最小单位固定为 1 字节 |
| 非字节对齐的捕获 | 参数起始位置必须字节对齐 |

### 9.2 性能特征

| 场景 | 性能 |
|------|------|
| 比对字面量 | `O(1)` 次 `memcmp`，长度 ≤ 16 字节时通常内联展开 |
| 查找字面量 | `O(n)` 次 `memcmp`，`n` 为段长 |
| 捕获 | 零拷贝，仅写指针 + 长度 |

### 9.3 内存模型

- 序列节点：`malloc` 分配，`stride_seq_free()` 释放
- 字面量/查找目标：构建时 `malloc` 复制，`stride_seq_free()` 释放
- 参数：零拷贝，指向输入段内部，调用方负责输入段生命周期

---

## 十、与 v2 的区别

| 方面 | v2 | v3（2026-09-11） | v3（2026-09-15 简化） |
|------|----|-----------------|---------------------|
| 序列结构 | 数组 + 状态机 | 单链表 + 函数式构建 | 单链表 + 函数式构建 |
| 执行引擎 | 状态机驱动 | 通用引擎 `stride_seq_run()` | 通用引擎 `stride_seq_run()` |
| 单位 | 步长（比特/步） | 步长（比特/步）+ 步数 | 字节 |
| 比特操作 | 支持 | 支持 | **不支持** |
| 段长参数 | `segment_steps`（步数） | `segment_steps`（步数） | `segment_len`（字节数） |
| API 复杂度 | 中等 | 较高（需换算步数） | 低（直接用字节数） |

---

## 十一、常见问题

### Q1：为什么移除步长概念？

**A**：步长概念虽然灵活，但增加了 API 复杂度：调用方需要换算步数与字节数，且容易出错。实际使用中 99% 的场景都是 1 字节 = 1 步，因此将最小单位固定为 1 字节可显著简化 API。

### Q2：如果需要比特级操作怎么办？

**A**：Stride 定位为字节级匹配/提取库。如果需要比特级操作，建议使用位操作库或自行实现。

### Q3：如何迁移现有的步长代码？

**A**：
1. 将所有 `stride` 参数移除
2. 将 `segment_steps` 改为 `segment_len`（字节数）
3. 将 `steps` 字段/参数改为 `bytes` 或 `len`
4. 移除 `stride_seq_skip_bits()` 调用，改用 `stride_seq_find_fwd()` 跳过字面量
