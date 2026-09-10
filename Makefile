# ============================================================
# Stride — 序列模式编译器
#
#   make            构建静态库 libstride.a
#   make test       构建并运行全部单元测试
#   make example    构建示例程序
#   make run        运行示例程序
#   make clean      清理构建产物
# ============================================================

CC      ?= cc
AR      ?= ar
CFLAGS  ?= -Wall -Wextra -O2 -g -std=c99
CPPFLAGS += -Iinclude \
            -Imodules/feature/include \
            -Imodules/extractor/include \
            -Imodules/compiler/include

BUILD_DIR := build

# ==================== 模块源文件 ====================

FEATURE_SRCS   := modules/feature/src/feature.c

EXTRACTOR_SRCS := modules/extractor/src/extractor_compile.c \
                  modules/extractor/src/extractor_execute.c

COMPILER_SRCS  := modules/compiler/src/grammar.c \
                  modules/compiler/src/compiler.c

MODULE_SRCS := $(FEATURE_SRCS) $(EXTRACTOR_SRCS) $(COMPILER_SRCS)

LIB      := $(BUILD_DIR)/libstride.a
LIB_OBJS := $(MODULE_SRCS:%.c=$(BUILD_DIR)/%.o)

# ==================== 测试 ====================

TEST_NAMES := test_grammar test_feature test_extractor test_compiler test_execute
TEST_SRCS  := $(addprefix tests/,$(addsuffix .c,$(TEST_NAMES)))
TEST_BINS  := $(addprefix $(BUILD_DIR)/,$(TEST_NAMES))

# ==================== 示例 ====================

EXAMPLE_BIN := $(BUILD_DIR)/example

# ==================== 构建规则 ====================

.PHONY: all lib test example run clean

all: lib

lib: $(LIB)

$(LIB): $(LIB_OBJS)
	@mkdir -p $(dir $@)
	$(AR) rcs $@ $^

$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

# 测试可执行文件
$(BUILD_DIR)/test_%: tests/test_%.c $(LIB)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ $< $(LIB)

example: $(EXAMPLE_BIN)

$(EXAMPLE_BIN): examples/example.c $(LIB)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ $< $(LIB)

run: $(EXAMPLE_BIN)
	$(EXAMPLE_BIN)

test: $(TEST_BINS)
	@failed=0; \
	for t in $(TEST_BINS); do \
		echo "=== $$t ==="; \
		$$t || failed=1; \
		echo ""; \
	done; \
	if [ $$failed -ne 0 ]; then \
		echo "*** 有测试失败 ***"; exit 1; \
	else \
		echo "*** 全部测试通过 ***"; \
	fi

clean:
	rm -rf $(BUILD_DIR)
