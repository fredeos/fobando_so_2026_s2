# ============================================================
# TOOLS AND BUILD MODE
# ============================================================

CC := gcc
MODE ?= debug

ESP_IDF := idf.py
CMAKE := cmake
BASH := bash

# ============================================================
# DIRECTORIES
# ============================================================

SRC_DIR := src
BUILD_DIR := build
OUT_DIR := output

HOST_SRC_DIR   := $(SRC_DIR)/host
COMMON_SRC_DIR := $(SRC_DIR)/common
ESP_SRC_DIR    := $(SRC_DIR)/nodes/esp32
PICO_SRC_DIR   := $(SRC_DIR)/nodes/pico

HOST_BUILD_DIR  := $(BUILD_DIR)/host
ESP_BUILD_DIR   := $(BUILD_DIR)/esp32
PICO_BUILD_DIR  := $(BUILD_DIR)/pico


# ============================================================
# MAKE VARIABLES
# ============================================================

DEV ?= USB0
IDF_ACTIVATE := $(HOME)/.espressif/tools/activate_idf_v6.1.sh
export PICO_SDK_PATH := $(HOME)/pico-sdk

# ============================================================
# EXECUTABLE TARGETS
# ============================================================

TARGET := $(OUT_DIR)/host

FOUNDATION_TEST := $(OUT_DIR)/test_foundation
PLANNER_TEST := $(OUT_DIR)/test_planner
CONFIG_TEST := $(OUT_DIR)/test_config
TASK_PARSER_TEST := $(OUT_DIR)/test_task_parser
FAILOVER_TEST := $(OUT_DIR)/test_failover
MANUAL_TEST := $(OUT_DIR)/test_manual_tasks

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

# Compilar únicamente Host y módulos comunes
SOURCE_DIRS := $(wildcard \
    $(HOST_SRC_DIR) \
    $(COMMON_SRC_DIR))

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

MANUAL_OBJ := $(BUILD_DIR)/host/planner/manual_tasks.o

# ============================================================
# TEST OBJECTS
# ============================================================

FOUNDATION_TEST_OBJ := $(BUILD_DIR)/tests/test_foundation.o

PLANNER_TEST_OBJ := $(BUILD_DIR)/tests/test_planner.o

CONFIG_TEST_OBJ := $(BUILD_DIR)/tests/test_config.o

TASK_PARSER_TEST_OBJ := $(BUILD_DIR)/tests/test_task_parser.o

FAILOVER_TEST_OBJ := $(BUILD_DIR)/tests/test_failover.o

MANUAL_TEST_OBJ := $(BUILD_DIR)/tests/test_manual_tasks.o

TEST_OBJS := \
    $(FOUNDATION_TEST_OBJ) \
    $(PLANNER_TEST_OBJ) \
    $(CONFIG_TEST_OBJ) \
    $(TASK_PARSER_TEST_OBJ) \
    $(FAILOVER_TEST_OBJ) \
    $(MANUAL_TEST_OBJ)

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
# MANUAL TASKS TEST
# ============================================================

$(MANUAL_TEST): $(MANUAL_TEST_OBJ) \
                $(PLANNER_OBJS) \
                $(MANUAL_OBJ)
	@mkdir -p $(dir $@)
	$(CC) $^ -o $@ $(LDLIBS)

# ============================================================
# RUN ALL TESTS
# ============================================================

test: $(FOUNDATION_TEST) \
      $(PLANNER_TEST) \
      $(CONFIG_TEST) \
      $(TASK_PARSER_TEST) \
      $(FAILOVER_TEST) \
      $(MANUAL_TEST)
	./$(FOUNDATION_TEST)
	./$(PLANNER_TEST)
	./$(CONFIG_TEST)
	./$(TASK_PARSER_TEST)
	./$(FAILOVER_TEST)
	./$(MANUAL_TEST)

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
# MCU CONFIGS
# ============================================================

mcu-config:
	@mkdir -p $(BUILD_DIR)
	@echo "Setting up PICO_SDK_PATH enviroment variable"
	@echo "PICO_SDK_PATH=$$PICO_SDK_PATH"
	@echo "Setting up ESP device..."
	bash -c 'source "$(IDF_ACTIVATE)" && $(ESP_IDF) -C $(ESP_SRC_DIR) -B $(ESP_BUILD_DIR) set-target esp32'
	@echo ""

# ============================================================
# ESP: make rules
# ============================================================

esp-build:
	@mkdir -p $(dir ${ESP_BUILD_DIR})
	@echo "Building ESP32 project files..."
	bash -c 'source "$(IDF_ACTIVATE)" && $(ESP_IDF) -C $(ESP_SRC_DIR) -B $(ESP_BUILD_DIR) build'
	@echo "[DONE]"
	@echo ""

esp-flash:
	@echo "Flashing ESP32..."
	bash -c 'source "$(IDF_ACTIVATE)" && $(ESP_IDF) -C $(ESP_SRC_DIR) -B $(ESP_BUILD_DIR) -p /dev/tty$(DEV) flash'
	@echo "[DONE]"
	@echo ""

# ============================================================
# Pico: make rules
# ============================================================

pico-build:
	@mkdir -p $(dir ${PICO_BUILD_DIR})
	@echo "Building Pi Pico project files..."
	cmake -S $(PICO_SRC_DIR) -B $(PICO_BUILD_DIR) -G "Unix Makefiles" -DPICO_BOARD=pico
	@echo "Generating Pi Pico binaries..."
	cmake --build $(PICO_BUILD_DIR)
	@echo "[DONE]"

picow-build:
	@mkdir -p $(dir ${PICO_BUILD_DIR})
	@echo "Building Pi Pico project files..."
	cmake -S $(PICO_SRC_DIR) -B $(PICO_BUILD_DIR)_w -G "Unix Makefiles" -DPICO_BOARD=pico_w
	@echo "Generating Pi Pico binaries..."
	cmake --build $(PICO_BUILD_DIR)_w
	@echo "[DONE]"

# ============================================================
# CLEAN
# ============================================================

clean:
	rm -rf build output

# ============================================================
# INCLUDE AUTOMATIC DEPENDENCIES
# ============================================================

-include $(DEPS)