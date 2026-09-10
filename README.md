# Stride

**Stride** 是一个用 C99 编写的轻量级**序列模式编译器**，零外部依赖，仅使用标准 C 库。

它把一段“序列模式”（例如 `$'v'${'.'}$'.'${}`）一次编译为两个相互独立、各司其职的产物：

| 产物 | 用途 | 特点 |
|------|------|------|
| **特征序列**（Feature Sequence） | 匹配 | 只保留“移动 + 关键字”，用于快速判定与分支区分 |
| **提取序列**（Extraction Sequence） | 参数提取 | 保留完整捕获语义，并在编译期做常量合并优化 |

匹配与提取分离，使匹配阶段无需关心捕获细节，提取阶段无需重复验证关键字。

> **与分隔符无关**：Stride 编译的是**单个段（segment）**——一个不透明的字符数组。
> 如何把输入切分为段（例如按 `/` 切分 URL 路径、按 `.` 切分域名、按 `,` 切分 CSV 行）
> 完全由调用者负责，不属于 Stride 的职责。因此 Stride 既可用于 URL 路由，
> 也可用于任何“按固定分隔符切分后逐段匹配并提取”的场景。

---

## 三个独立模块

Stride 由三个**彼此独立、可单独编译与测试**的模块组成：

| 模块 | 目录 | 公共头文件 | 职责 |
|------|------|-----------|------|
| **编译器**（compiler） | `modules/compiler/` | `stride/compiler.h`、`stride/grammar.h` | 序列编译入口 + 语法（词法分析） |
| **特征序列**（feature） | `modules/feature/` | `stride/feature.h` | 操作符序列 → 特征序列（匹配用） |
| **提取序列**（extractor） | `modules/extractor/` | `stride/extractor.h` | 操作符序列 → 提取序列（编译）+ 运行时提取 |

三个模块共同依赖一个中立的**共享契约层** `include/stride/core.h`（操作符 IR 与状态码），
依赖关系严格无环：

```
                    core.h            （操作符 IR + 状态码）
                  /    |    \
       feature.h   extractor.h   grammar.h
                  \    |    /
                  compiler.h          （编排三个部分）
```

- `feature` 与 `extractor` 都只依赖 `core.h`，彼此**互不依赖**；
- `grammar`（位于 compiler 模块）只依赖 `core.h`；
- `compiler` 依赖全部三者，负责编排与生命周期管理。

因此可以只用“语法 + 特征序列”做纯匹配，也可以只用“语法 + 提取序列”做纯提取。

---

## 目录结构

```
Stride/
├── include/stride/
│   ├── core.h              # 共享契约：操作符 IR、状态码
│   └── stride.h            # 总入口（聚合全部公共头文件）
├── modules/
│   ├── compiler/           # 模块三：编译器（含语法）
│   │   ├── include/stride/
│   │   │   ├── grammar.h   # 词法分析 API
│   │   │   └── compiler.h  # 序列编译入口
│   │   └── src/
│   │       ├── grammar.c
│   │       └── compiler.c
│   ├── feature/            # 模块一：特征序列
│   │   ├── include/stride/feature.h
│   │   └── src/feature.c
│   └── extractor/          # 模块二：提取序列
│       ├── include/stride/extractor.h
│       └── src/
│           ├── extractor_compile.c   # 编译 + 优化
│           └── extractor_execute.c   # 运行时提取
├── tests/                  # 单元测试与集成测试
├── examples/example.c      # 使用示例
├── doc/                    # 设计文档（中文）
├── Makefile
└── README.md
```

---

## 快速开始

### 构建

```bash
make            # 构建静态库 build/libstride.a
make test       # 构建并运行全部单元测试
make example    # 构建示例
make run        # 运行示例
make clean      # 清理
```

### 最小示例

```c
#include <stdio.h>
#include "stride/stride.h"

int main(void) {
    /* 编译单个段模式：$'v'${'.'}$'.'${}  */
    stride_compile_result_t r = stride_compile("$'v'${'.'}$'.'${}");

    if (r.status != STRIDE_OK) {
        printf("compile error: %s\n", stride_status_str(r.status));
        stride_compile_free(&r);
        return 1;
    }

    /* 用提取序列在段 "v2.0" 上提取参数 */
    stride_extractor_t *ex =
        stride_extractor_create(r.extractors, r.extractor_count);

    stride_param_t params[8];
    size_t count = 0;
    if (stride_extractor_execute(ex, "v2.0", 4, params, 8, &count) == 0) {
        for (size_t i = 0; i < count; i++)
            printf("[%zu] %.*s\n", i, (int)params[i].len, params[i].ptr);
        /* 输出：[0] 2    [1] 0 */
    }

    stride_extractor_destroy(ex);
    stride_compile_free(&r);
    return 0;
}
```

编译链接：

```bash
cc -std=c99 -Iinclude -Imodules/feature/include \
   -Imodules/extractor/include -Imodules/compiler/include \
   app.c build/libstride.a -o app
```

参数以 `stride_param_t { const char *ptr; size_t len; }` 返回，**零拷贝**：
`ptr` 直接指向输入段内部，调用者只需保证输入在使用期间有效。

---

## 语法

段模式由以下 9 类操作符按书写顺序组成（共 10 个 `stride_op_type_t` 枚举值）：

| 类别 | 操作符 | 说明 |
|------|--------|------|
| 匹配 | `$'文本'` | 精确匹配固定字符串 |
| 捕获 | `${数字}` | 捕获指定长度字符 |
| 捕获 | `${'字符'}` | 捕获到指定字符前（不含该字符） |
| 捕获 | `${}` | 捕获到段尾 |
| 移动 | `$[位置]` | 绝对跳转到某位置（基于 HEAD） |
| 移动 | `$[END]` / `$[END-n]` | 跳到段尾 / 从段尾向前偏移 n |
| 移动 | `$[>偏移]` | 向段尾方向移动 |
| 移动 | `$[<偏移]` | 向段首方向移动 |
| 移动 | `$[>'字符']` | 向段尾方向查找字符 |
| 移动 | `$[<'字符']` | 向段首方向查找字符 |

匹配成功需同时满足：**操作耗尽** 且 **指针恰好位于段尾**（段尾对齐，避免部分匹配）。

完整语法、边界规则与示例见 [`doc/Stride 语法规范.md`](doc/Stride%20语法规范.md)。

---

## API 速览

### 编译器（`stride/compiler.h`）

| 函数 | 说明 |
|------|------|
| `stride_compile_result_t stride_compile(const char *pattern)` | 编译单个段模式，一次产出特征序列与提取序列 |
| `void stride_compile_free(stride_compile_result_t *result)` | 释放编译结果 |

`stride_compile_result_t` 字段：`status`、`features`/`feature_count`、
`extractors`/`extractor_count`、`param_count`、`error_msg`、`error_pos`。

### 语法 / 词法（`stride/grammar.h`）

| 函数 | 说明 |
|------|------|
| `int stride_lex(const char *pattern, stride_op_t **out_ops, size_t *out_count, size_t *out_capacity)` | 模式字符串 → 操作符序列 |
| `void stride_ops_free(stride_op_t *ops)` | 释放操作符序列 |

> `STRIDE_OP_MATCH` 的 `data.match.text` 指向传入的 `pattern`，不复制；
> 调用者需保证 `pattern` 在操作符序列使用期间有效。

### 特征序列（`stride/feature.h`）

| 函数 | 说明 |
|------|------|
| `int stride_feature_compile(const stride_op_t *ops, size_t op_count, stride_feature_t **out_features, size_t *out_count, size_t *out_capacity)` | 操作符序列 → 特征序列 |
| `void stride_feature_free(stride_feature_t *features, size_t count)` | 释放特征序列（含关键字副本） |

特征元组 6 种类型：`STRIDE_FT_CONST_REL_FWD`、`STRIDE_FT_CONST_REL_BACK`、
`STRIDE_FT_CONST_ABS_HEAD`、`STRIDE_FT_CONST_ABS_END`、
`STRIDE_FT_DYNAMIC_FIND_FWD`、`STRIDE_FT_DYNAMIC_FIND_REV`。

### 提取序列（`stride/extractor.h`）

| 函数 | 说明 |
|------|------|
| `int stride_extractor_compile(...)` | 操作符序列 → 提取序列（含优化） |
| `stride_extractor_t *stride_extractor_create(const stride_extractor_op_t *ops, size_t op_count)` | 创建单段提取器 |
| `void stride_extractor_destroy(stride_extractor_t *ex)` | 销毁单段提取器 |
| `int stride_extractor_execute(const stride_extractor_t *ex, const char *segment, size_t segment_len, stride_param_t *params, size_t param_capacity, size_t *param_count)` | 在段上执行提取 |
| `stride_full_extractor_t *stride_full_extractor_create(stride_extractor_t **segs, size_t n)` | 组合多段提取器 |
| `void stride_full_extractor_destroy(stride_full_extractor_t *full)` | 销毁多段提取器（含各段） |
| `int stride_full_extractor_execute(...)` | 多段提取，参数按段顺序连接 |

提取操作 10 种：`STRIDE_EX_CAPTURE_LEN/CHR/END`（产生参数）、
`STRIDE_EX_SKIP_LEN`、`STRIDE_EX_JUMP_ABS/END/FWD/BACK`、
`STRIDE_EX_FIND_FWD/REV`（不产生参数）。

---

## 编译模型

```
模式字符串
   │  ① 词法分析（grammar）
   ▼
操作符序列（stride_op_t）
   ├──② 特征序列编译（feature）  → 特征序列：用于匹配
   └──③ 提取序列编译（extractor）→ 提取序列：用于参数提取（编译期优化）
```

- **特征序列编译**：IDLE / HOLD 两状态机。常量操作可相加，关键字与当前 HOLD 元组合并，
  动态操作打断合并。只保留匹配所需信息，丢弃捕获边界。
- **提取序列编译**：两阶段。① 基础转换：`OP_MATCH → STRIDE_EX_SKIP_LEN`（匹配阶段已
  验证过关键字，提取阶段无需重复验证）；② 常量移动合并：连续的
  `EX_SKIP_LEN`/`EX_JUMP_FWD`/`EX_JUMP_BACK` 相加抵消为一个操作。

详细状态转换表、常量相加 / END 约束 / 关键字合并规则见
[`doc/Stride 编译器设计文档.md`](doc/Stride%20编译器设计文档.md) 与
[`doc/Stride 特征序列设计文档.md`](doc/Stride%20特征序列设计文档.md)。

---

## 测试

```bash
make test
```

测试按模块拆分，每个模块独立可执行：

| 测试 | 覆盖内容 |
|------|---------|
| `tests/test_grammar.c` | 10 种操作符的词法分析、语法错误处理 |
| `tests/test_feature.c` | 常量相加、动态打断、关键字合并、END/HEAD 元组 |
| `tests/test_extractor.c` | 匹配优化、常量合并、抵消、对象生命周期 |
| `tests/test_compiler.c` | `stride_compile` 端到端、参数计数一致性、错误码 |
| `tests/test_execute.c` | 运行时提取、零拷贝、反向查找、回溯、多段提取、边界失败 |

---

## 设计文档

- [Stride 语法规范](doc/Stride%20语法规范.md) —— 操作符语法、执行模型、边界检查、完整示例
- [Stride 特征序列设计文档](doc/Stride%20特征序列设计文档.md) —— 六种元组、HEAD/END 基准、编译规则
- [Stride 编译器设计文档](doc/Stride%20编译器设计文档.md) —— 词法表、状态机、两阶段编译、错误码

---

## 与 URLRouter 的关系

Stride 抽取自 [URLRouter](../URLRouter) 的编译器子系统（语法解析 / 特征序列 / 提取序列），
并做了如下调整：

1. **URL 无关化**：只保留“单段模式编译”，URL 切分、路由树、HTTP 方法、回调等全部留给调用者。
2. **命名空间化**：公共符号统一加 `stride_` / `STRIDE_` 前缀，避免与宿主项目冲突。
3. **模块化**：按“编译器 / 特征序列 / 提取序列”拆为三个独立模块，依赖无环。
4. **修复一处越界优化缺陷**：原提取序列实现在“净位移为 0 且包含 `EX_SKIP_LEN`”时
   （例如 `$'abc'$[<3]`）会输出一个未初始化的操作——`calloc` 后的零值恰好等于
   `EX_CAPTURE_LEN`，导致凭空多出一个长度为 0 的参数。Stride 中净位移为 0 的合并段
   直接丢弃（语义上是无操作），并新增回归测试。

---

## 许可证

本项目采用 MIT 许可证，见 [LICENSE](LICENSE)。
