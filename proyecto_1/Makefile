# ============================================================
# TOOLS AND BUILD MODE
# ============================================================

CC := gcc
MODE ?= debug

# ============================================================
# DIRECTORIES
# ============================================================

SRC_DIR := src
BUILD_DIR := build/$(MODE)
OUT_DIR := output/$(MODE)

# ============================================================
# EXECUTABLE TARGETS
# ============================================================

TARGET := $(OUT_DIR)/host

FOUNDATION_TEST := $(OUT_DIR)/test_foundation
PLANNER_TEST := $(OUT_DIR)/test_planner
CONFIG_TEST := $(OUT_DIR)/test_config
TASK_PARSER_TEST := $(OUT_DIR)/test_task_parser
FAILOVER_TEST := $(OUT_DIR)/test_failover

# ============================================================
# COMPILER FLAGS
# ============================================================

CFLAGS := -Wall -Wextra -Wpedantic \
          -std=c17 \
          -D_POSIX_C_SOURCE=200809L \
          -MMD -MP \
          -Iinclude

ifeq ($(MODE),release)
    CFLAGS += -O2
else
    CFLAGS += -O0 -g3
endif

# ============================================================
# LINKER
# ============================================================

LDLIBS := -pthread -lm

# ============================================================
# SOURCE FILES
# ============================================================

# Compile only Host and common modules
SOURCE_DIRS := $(wildcard \
    $(SRC_DIR)/host \
    $(SRC_DIR)/common)

SOURCES := $(foreach dir,$(SOURCE_DIRS),\
    $(shell find $(dir) -type f -name '*.c'))

OBJS := $(patsubst \
    $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(SOURCES))

# ============================================================
# PLANNER DEPENDENCIES
# ============================================================

PLANNER_SOURCES := \
    $(SRC_DIR)/host/planner/node.c \
    $(SRC_DIR)/host/planner/admission.c \
    $(SRC_DIR)/host/planner/planner.c

PLANNER_OBJS := $(patsubst \
    $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(PLANNER_SOURCES))

FAILOVER_OBJ := $(BUILD_DIR)/host/planner/failover.o

# ============================================================
# TEST OBJECTS
# ============================================================

FOUNDATION_TEST_OBJ := $(BUILD_DIR)/tests/test_foundation.o
PLANNER_TEST_OBJ := $(BUILD_DIR)/tests/test_planner.o
CONFIG_TEST_OBJ := $(BUILD_DIR)/tests/test_config.o
TASK_PARSER_TEST_OBJ := $(BUILD_DIR)/tests/test_task_parser.o
FAILOVER_TEST_OBJ := $(BUILD_DIR)/tests/test_failover.o

TEST_OBJS := \
    $(FOUNDATION_TEST_OBJ) \
    $(PLANNER_TEST_OBJ) \
    $(CONFIG_TEST_OBJ) \
    $(TASK_PARSER_TEST_OBJ) \
    $(FAILOVER_TEST_OBJ)

# ============================================================
# AUTOMATIC DEPENDENCIES
# ============================================================

DEPS := $(sort \
    $(OBJS:.o=.d) \
    $(TEST_OBJS:.o=.d))

# ============================================================
# PHONY TARGETS
# ============================================================

.PHONY: all run demo debug release test clean

# ============================================================
# DEFAULT BUILD
# ============================================================

all: $(TARGET)

# ============================================================
# LINK HOST
# ============================================================

$(TARGET): $(OBJS)
	@mkdir -p $(dir $@)
	$(CC) $^ -o $@ $(LDLIBS)

# ============================================================
# COMPILE HOST AND COMMON SOURCES
# ============================================================

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# ============================================================
# COMPILE TEST SOURCES
# ============================================================

$(BUILD_DIR)/tests/%.o: tests/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# ============================================================
# FOUNDATION TEST
# ============================================================

$(FOUNDATION_TEST): $(FOUNDATION_TEST_OBJ) \
                    $(BUILD_DIR)/common/protocol.o
	@mkdir -p $(dir $@)
	$(CC) $^ -o $@ $(LDLIBS)

# ============================================================
# PLANNER TEST
# ============================================================

$(PLANNER_TEST): $(PLANNER_TEST_OBJ) $(PLANNER_OBJS)
	@mkdir -p $(dir $@)
	$(CC) $^ -o $@ $(LDLIBS)

# ============================================================
# CONFIG TEST
# ============================================================

$(CONFIG_TEST): $(CONFIG_TEST_OBJ) \
                $(BUILD_DIR)/host/config/config.o
	@mkdir -p $(dir $@)
	$(CC) $^ -o $@ $(LDLIBS)

# ============================================================
# TASK PARSER TEST
# ============================================================

$(TASK_PARSER_TEST): $(TASK_PARSER_TEST_OBJ) \
                     $(BUILD_DIR)/host/config/task_parser.o
	@mkdir -p $(dir $@)
	$(CC) $^ -o $@ $(LDLIBS)

# ============================================================
# FAILOVER TEST
# ============================================================

$(FAILOVER_TEST): $(FAILOVER_TEST_OBJ) \
                  $(PLANNER_OBJS) \
                  $(FAILOVER_OBJ)
	@mkdir -p $(dir $@)
	$(CC) $^ -o $@ $(LDLIBS)

# ============================================================
# RUN ALL TESTS
# ============================================================

test: $(FOUNDATION_TEST) \
      $(PLANNER_TEST) \
      $(CONFIG_TEST) \
      $(TASK_PARSER_TEST) \
      $(FAILOVER_TEST)
	./$(FOUNDATION_TEST)
	./$(PLANNER_TEST)
	./$(CONFIG_TEST)
	./$(TASK_PARSER_TEST)
	./$(FAILOVER_TEST)

# ============================================================
# RUN HOST
# ============================================================

run: $(TARGET)
	./$(TARGET)

# ============================================================
# RUN PLANNER DEMO
# ============================================================

demo: $(TARGET)
	./$(TARGET) --demo config/system.conf 100 40

# ============================================================
# DEBUG BUILD
# ============================================================

debug:
	$(MAKE) MODE=debug all

# ============================================================
# RELEASE BUILD
# ============================================================

release:
	$(MAKE) MODE=release all

# ============================================================
# CLEAN
# ============================================================

clean:
	rm -rf build output

# ============================================================
# INCLUDE AUTOMATIC DEPENDENCIES
# ============================================================

-include $(DEPS)