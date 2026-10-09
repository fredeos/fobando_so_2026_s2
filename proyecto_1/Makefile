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
# TARGETS
# ============================================================

TARGET := $(OUT_DIR)/host

FOUNDATION_TEST := $(OUT_DIR)/test_foundation
PLANNER_TEST := $(OUT_DIR)/test_planner

# ============================================================
# COMPILER AND LINKER FLAGS
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

LDLIBS := -pthread -lm

# ============================================================
# SOURCE FILES
# ============================================================

# Compile Host and common modules only
SOURCE_DIRS := $(wildcard $(SRC_DIR)/host $(SRC_DIR)/common)

SOURCES := $(foreach dir,$(SOURCE_DIRS),$(shell find $(dir) -type f -name '*.c'))

OBJS := $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(SOURCES))

# ============================================================
# PLANNER TEST DEPENDENCIES
# ============================================================

PLANNER_SOURCES := \
    $(SRC_DIR)/host/planner/node.c \
    $(SRC_DIR)/host/planner/admission.c \
    $(SRC_DIR)/host/planner/planner.c

PLANNER_OBJS := $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(PLANNER_SOURCES))

# ============================================================
# TEST OBJECTS
# ============================================================

FOUNDATION_TEST_OBJ := $(BUILD_DIR)/tests/test_foundation.o
PLANNER_TEST_OBJ := $(BUILD_DIR)/tests/test_planner.o

# ============================================================
# AUTOMATIC DEPENDENCIES
# ============================================================

DEPS := $(OBJS:.o=.d) \
        $(FOUNDATION_TEST_OBJ:.o=.d) \
        $(PLANNER_TEST_OBJ:.o=.d)

# ============================================================
# PHONY TARGETS
# ============================================================

.PHONY: all run debug release test clean

# ============================================================
# DEFAULT BUILD
# ============================================================

all: $(TARGET)

# ============================================================
# LINK HOST
# ============================================================

$(TARGET): $(OBJS)
	@mkdir -p $(dir $@)
	$(CC) $(OBJS) -o $@ $(LDLIBS)

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
# RUN TESTS
# ============================================================

test: $(FOUNDATION_TEST) $(PLANNER_TEST)
	./$(FOUNDATION_TEST)
	./$(PLANNER_TEST)

# ============================================================
# RUN HOST
# ============================================================

run: $(TARGET)
	./$(TARGET)

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