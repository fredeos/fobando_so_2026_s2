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
ESP_BUILD_DIR   := $(ESP_SRC_DIR)/build/esp32
PICO_BUILD_DIR  := $(PICO_SRC_DIR)/build/pico


# ============================================================
# MAKE VARIABLES
# ============================================================

DEV ?= USB0
IDF_ACTIVATE ?= $(HOME)/esp/esp-idf/export.sh
ESP_ALGORITHM ?= RMS
ESP_TICK_MS ?= 1000
ESP_LOCAL_TEST ?= ON
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
MESSAGE_PARSER_TEST := $(OUT_DIR)/test_message_parser
EVENT_QUEUE_TEST := $(OUT_DIR)/test_event_queue
REPORT_TEST := $(OUT_DIR)/test_report
REPORT_SESSION_TEST := $(OUT_DIR)/test_report_session

# ============================================================
# COMPILER FLAGS
# ============================================================

CFLAGS := -Wall -Wextra -Wpedantic \
          -std=c17 \
          -D_POSIX_C_SOURCE=200809L \
          -MMD -MP \
          -Iinclude \
          -Isrc/host \
          -Ithird_party/coreJSON \
          -Isrc/nodes/scheduler

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

# Biblioteca JSON común a Linux, ESP32 y Pico
CORE_JSON_OBJ := $(BUILD_DIR)/third_party/coreJSON/core_json.o
OBJS += $(CORE_JSON_OBJ)

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
MESSAGE_PARSER_TEST_OBJ := $(BUILD_DIR)/tests/test_message_parser.o
EVENT_QUEUE_TEST_OBJ := $(BUILD_DIR)/tests/test_event_queue.o
REPORT_TEST_OBJ := $(BUILD_DIR)/tests/test_report.o
REPORT_SESSION_TEST_OBJ := $(BUILD_DIR)/tests/test_report_session.o

EVENT_QUEUE_OBJ := $(BUILD_DIR)/host/ipc/event_queue.o
REPORT_OBJS := $(BUILD_DIR)/host/report/report.o \
               $(BUILD_DIR)/host/report/statistics.o \
               $(BUILD_DIR)/host/report/gantt.o

SCHEDULER_TEST_OBJ := $(BUILD_DIR)/nodes/node_tests/test_scheduler.o

SCHEDULER_OBJ := $(BUILD_DIR)/nodes/scheduler/scheduler.o

RMS_TEST_OBJ := $(BUILD_DIR)/nodes/node_tests/test_rms.o

RMS_OBJ := $(BUILD_DIR)/nodes/scheduler/rms.o

UT_OBJ := $(BUILD_DIR)/nodes/scheduler/ut.o

EDF_TEST_OBJ := $(BUILD_DIR)/nodes/node_tests/test_edf.o

EDF_OBJ := $(BUILD_DIR)/nodes/scheduler/edf.o

TEST_OBJS := \
    $(FOUNDATION_TEST_OBJ) \
    $(PLANNER_TEST_OBJ) \
    $(CONFIG_TEST_OBJ) \
    $(TASK_PARSER_TEST_OBJ) \
    $(FAILOVER_TEST_OBJ) \
    $(MANUAL_TEST_OBJ) \
    $(MESSAGE_PARSER_TEST_OBJ) \
    $(EVENT_QUEUE_TEST_OBJ) \
    $(REPORT_TEST_OBJ) \
    $(REPORT_SESSION_TEST_OBJ)
	

# ============================================================
# AUTOMATIC DEPENDENCIES
# ============================================================

DEPS := $(sort \
    $(OBJS:.o=.d) \
    $(TEST_OBJS:.o=.d) \
    $(SCHEDULER_TEST_OBJ:.o=.d) \
    $(SCHEDULER_OBJ:.o=.d) \
    $(RMS_TEST_OBJ:.o=.d) \
    $(RMS_OBJ:.o=.d) \
    $(EDF_TEST_OBJ:.o=.d) \
    $(EDF_OBJ:.o=.d))

# ============================================================
# PHONY TARGETS
# ============================================================

.PHONY: all run demo reports-demo debug release test test-scheduler test-rms test-edf clean

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

$(CORE_JSON_OBJ): third_party/coreJSON/core_json.c
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
# SCHEDULER TEST
# ============================================================

$(SCHEDULER_TEST): $(SCHEDULER_TEST_OBJ) \
                   $(SCHEDULER_OBJ) \
                   $(RMS_OBJ) \
                   $(EDF_OBJ) \
                   $(UT_OBJ)
	@mkdir -p $(dir $@)
	$(CC) $^ -o $@ $(LDLIBS)

# ============================================================
# RMS TEST
# ============================================================

$(RMS_TEST): $(RMS_TEST_OBJ) \
             $(RMS_OBJ) \
             $(SCHEDULER_OBJ) \
             $(EDF_OBJ) \
             $(UT_OBJ)
	@mkdir -p $(dir $@)
	$(CC) $^ -o $@ $(LDLIBS)

# ============================================================
# EDF TEST
# ============================================================

$(EDF_TEST): $(EDF_TEST_OBJ) \
             $(EDF_OBJ) \
             $(SCHEDULER_OBJ) \
             $(RMS_OBJ) \
             $(UT_OBJ)
	@mkdir -p $(dir $@)
	$(CC) $^ -o $@ $(LDLIBS)

# ============================================================
# RUN ALL TESTS
# ============================================================

# Parser de contenido, independiente del protocolo serial
$(MESSAGE_PARSER_TEST): $(MESSAGE_PARSER_TEST_OBJ) \
                        $(BUILD_DIR)/common/message_parser.o \
                        $(CORE_JSON_OBJ)
	@mkdir -p $(dir $@)
	$(CC) $^ -o $@ $(LDLIBS)

# Cola compartida, reconstrucción de reportes y coordinación de cierre
$(EVENT_QUEUE_TEST): $(EVENT_QUEUE_TEST_OBJ) $(EVENT_QUEUE_OBJ)
	@mkdir -p $(dir $@)
	$(CC) $^ -o $@ $(LDLIBS)

$(REPORT_TEST): $(REPORT_TEST_OBJ) $(EVENT_QUEUE_OBJ) $(REPORT_OBJS)
	@mkdir -p $(dir $@)
	$(CC) $^ -o $@ $(LDLIBS)

$(REPORT_SESSION_TEST): $(REPORT_SESSION_TEST_OBJ) $(EVENT_QUEUE_OBJ) \
                        $(REPORT_OBJS) $(BUILD_DIR)/host/report/session.o
	@mkdir -p $(dir $@)
	$(CC) $^ -o $@ $(LDLIBS)

test: $(FOUNDATION_TEST) \
      $(PLANNER_TEST) \
      $(CONFIG_TEST) \
      $(TASK_PARSER_TEST) \
      $(FAILOVER_TEST) \
      $(MANUAL_TEST) \
      $(SCHEDULER_TEST) \
      $(RMS_TEST) \
      $(EDF_TEST) \
      $(MESSAGE_PARSER_TEST) \
      $(EVENT_QUEUE_TEST) \
      $(REPORT_TEST) \
      $(REPORT_SESSION_TEST)
	./$(FOUNDATION_TEST)
	./$(PLANNER_TEST)
	./$(CONFIG_TEST)
	./$(TASK_PARSER_TEST)
	./$(FAILOVER_TEST)
	./$(MANUAL_TEST)
	./$(MESSAGE_PARSER_TEST)
	./$(EVENT_QUEUE_TEST)
	./$(REPORT_TEST)
	./$(REPORT_SESSION_TEST)
	./$(SCHEDULER_TEST)
	./$(RMS_TEST)
	./$(EDF_TEST)

# ============================================================
# RUN SCHEDULER TEST
# ============================================================

test-scheduler: $(SCHEDULER_TEST)
	./$(SCHEDULER_TEST)

# ============================================================
# RUN RMS TEST
# ============================================================

.PHONY: test-rms

test-rms: $(RMS_TEST)
	./$(RMS_TEST)

# ============================================================
# RUN EDF TEST
# ============================================================

test-edf: $(EDF_TEST)
	./$(EDF_TEST)

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

# Dos productores simulados y un consumidor de reportes; q detiene la sesión
reports-demo: $(TARGET)
	./$(TARGET) --report-demo

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

# Usa un entorno ESP-IDF activo o IDF_ACTIVATE=/ruta/al/export.sh.
.PHONY: esp-build esp-flash esp-monitor test-esp-local
esp-build:
	@mkdir -p $(ESP_BUILD_DIR)
	bash -c 'if [ -f "$(IDF_ACTIVATE)" ]; then source "$(IDF_ACTIVATE)" || exit; fi; if ! command -v "$(ESP_IDF)" >/dev/null 2>&1; then echo "Active ESP-IDF o indique IDF_ACTIVATE=/ruta/al/export.sh"; exit 1; fi; $(ESP_IDF) -C $(ESP_SRC_DIR) -B $(ESP_BUILD_DIR) -DESP_LOCAL_TEST=$(ESP_LOCAL_TEST) -DLOCAL_ALGORITHM=$(ESP_ALGORITHM) -DLOCAL_TICK_MS=$(ESP_TICK_MS) build'

esp-flash:
	bash -c 'if [ -f "$(IDF_ACTIVATE)" ]; then source "$(IDF_ACTIVATE)" || exit; fi; if ! command -v "$(ESP_IDF)" >/dev/null 2>&1; then echo "Active ESP-IDF o indique IDF_ACTIVATE=/ruta/al/export.sh"; exit 1; fi; $(ESP_IDF) -C $(ESP_SRC_DIR) -B $(ESP_BUILD_DIR) -p /dev/tty$(DEV) flash'

esp-monitor:
	bash -c 'if [ -f "$(IDF_ACTIVATE)" ]; then source "$(IDF_ACTIVATE)" || exit; fi; if ! command -v "$(ESP_IDF)" >/dev/null 2>&1; then echo "Active ESP-IDF o indique IDF_ACTIVATE=/ruta/al/export.sh"; exit 1; fi; $(ESP_IDF) -C $(ESP_SRC_DIR) -B $(ESP_BUILD_DIR) -p /dev/tty$(DEV) monitor'

# La misma regresión de cinco tareas se ejecuta sobre el fixture del ESP32.
test-esp-local: $(OUT_DIR)/test_esp_local
	./$(OUT_DIR)/test_esp_local

$(OUT_DIR)/test_esp_local: tests/test_pico_local.c \
    src/nodes/esp32/main/local_test.h src/nodes/scheduler/scheduler.c \
    src/nodes/scheduler/scheduler.h src/nodes/scheduler/task.h \
    src/nodes/scheduler/rms.c src/nodes/scheduler/edf.c src/common/ut.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -Isrc/nodes/esp32/main tests/test_pico_local.c \
	    src/nodes/scheduler/scheduler.c src/nodes/scheduler/rms.c \
	    src/nodes/scheduler/edf.c src/common/ut.c -o $@ $(LDLIBS)

# ============================================================
# Pico: make rules
# ============================================================

pico-build:
	@mkdir -p $(PICO_BUILD_DIR)
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

# Prueba del fixture local de Pico en Linux, sin SDK ni hardware.
.PHONY: test-pico-local
test-pico-local: $(OUT_DIR)/test_pico_local
	./$(OUT_DIR)/test_pico_local

$(OUT_DIR)/test_pico_local: tests/test_pico_local.c \
    src/nodes/pico/local_test.h src/nodes/scheduler/scheduler.c \
    src/nodes/scheduler/scheduler.h src/nodes/scheduler/task.h \
    src/nodes/scheduler/rms.c src/nodes/scheduler/edf.c src/common/ut.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -Isrc/nodes/pico tests/test_pico_local.c \
	    src/nodes/scheduler/scheduler.c src/nodes/scheduler/rms.c \
	    src/nodes/scheduler/edf.c src/common/ut.c -o $@ $(LDLIBS)
