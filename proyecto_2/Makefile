# =============================================================================
# Makefile tools and build mode
# =============================================================================
CC := gcc
MODE ?= debug

ESP_IDF := idf.py

# =============================================================================
# Compiler flags
# =============================================================================
CFLAGS := -Wall -Wextra -Wpedantic \
          -std=c17 \
          -D_POSIX_C_SOURCE=200809L \
          -MMD -MP \
          -Iinclude \
		  -Ithird_party

ifeq ($(MODE),release)
    CFLAGS += -O2
else
    CFLAGS += -O0 -g3
endif

# =============================================================================
# Linker
# =============================================================================
LDLIBS := -pthread -lm

# =============================================================================
# Makefile variables
# =============================================================================
PORT ?= /dev/ttyUSB0

IDF_ACTIVATE ?= $(HOME)/.espressif/tools/activate_idf_v6.1.sh

PICO_SDK_PATH ?= $(HOME)/pico-sdk
export PICO_SDK_PATH

# =============================================================================
# Directories
# =============================================================================
SRC_DIR := src
OUT_DIR := output
BUILD_DIR := build
INCLUDE_DIR := include

COMMON_SRC := $(SRC_DIR)/common
3PARTY_SRC := third_party

CLIENT_SRC := $(SRC_DIR)/client
CLIENT_OUT := $(OUT_DIR)/client
CLIENT_BUILD := $(BUILD_DIR)/client

SERVER_SRC := $(SRC_DIR)/server
SERVER_OUT := $(OUT_DIR)/server
SERVER_BUILD := $(BUILD_DIR)/server

ESP_SRC := $(SRC_DIR)/firmware/esp32
ESP_BUILD := $(BUILD_DIR)/esp32

# =============================================================================
# Sources, objects and dependencies
# =============================================================================
# << Server sources, objects and dependencies >>

# Compilar únicamente fuente y módulos comunes
SERVER_SRC_DIRS := $(wildcard \
    $(SERVER_SRC) \
    $(COMMON_SRC) )

SERVER_SOURCES := $(foreach dir,$(SERVER_SRC_DIRS),\
    $(shell find $(dir) -type f -name '*.c'))

SERVER_OBJS := $(patsubst \
    $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(SERVER_SOURCES))

SERVER_DEPS := $(sort \
    $(SERVER_OBJS:.o=.d))

# << Client sources, objects and dependencies >>

# Compilar únicamente fuente y módulos comunes
CLIENT_SRC_DIRS := $(wildcard \
    $(CLIENT_SRC) \
    $(COMMON_SRC) \
	$(SRC_DIR)/serial \
	$(3PARTY_SRC) )

CLIENT_SOURCES := $(foreach dir,$(CLIENT_SRC_DIRS),\
    $(shell find $(dir) -type f -name '*.c'))

CLIENT_OBJS := $(patsubst \
    $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(filter $(SRC_DIR)/%.c,$(CLIENT_SOURCES))) \
    $(patsubst \
    $(3PARTY_SRC)/%.c,$(BUILD_DIR)/$(3PARTY_SRC)/%.o,$(filter $(3PARTY_SRC)/%.c,$(CLIENT_SOURCES)))

CLIENT_DEPS := $(sort \
    $(CLIENT_OBJS:.o=.d))


# =============================================================================
# Generic rules
# =============================================================================
.PHONY: clean dirs

dirs:
	@mkdir -p $(BUILD_DIR) $(OUT_DIR)

clean:
	rm -rf $(BUILD_DIR) $(OUT_DIR)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/$(3PARTY_SRC)/%.o: $(3PARTY_SRC)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# =============================================================================
# Makefile server rules and variables
# =============================================================================
.PHONY: server-build server-debug server-release

server-build: $(SERVER_OBJS)
	$(MAKE) dirs
	@mkdir -p $(dir ${SERVER_OUT}) $(dir ${SERVER_BUILD})
	$(CC) $(CFLAGS) $^ -o $(SERVER_OUT) $(LDLIBS)

server-debug:
	$(MAKE) MODE=debug server-build

server-release:
	$(MAKE) MODE=release server-build

# =============================================================================
# Makefile client rules and variables
# =============================================================================
.PHONY: client-build client-debug client-release

client-build: $(CLIENT_OBJS)
	$(MAKE) dirs
	@mkdir -p $(dir ${CLIENT_OUT}) $(dir ${CLIENT_BUILD})
	$(CC) $(CFLAGS) $^ -o $(CLIENT_OUT) $(LDLIBS)

client-debug:
	$(MAKE) MODE=debug client-build

client-release:
	$(MAKE) MODE=release client-build

# =============================================================================
# Makefile dependency tracking
# =============================================================================
-include $(SERVER_DEPS) $(CLIENT_DEPS)

# =============================================================================
# Makefile ESP32 rules and variables
# =============================================================================
.PHONY: esp-config esp-build esp-flash

esp-config: dirs
	@mkdir -p $(dir ${ESP_BUILD})
	@echo "Setting up ESP device..."
	bash -c 'source "$(IDF_ACTIVATE)" && $(ESP_IDF) -C $(ESP_SRC) -B $(ESP_BUILD) set-target esp32'
	@echo ""

esp-build:
	@mkdir -p $(dir ${ESP_BUILD})
	@echo "Building ESP32 project files..."
	bash -c 'source "$(IDF_ACTIVATE)" && $(ESP_IDF) -C $(ESP_SRC) -B $(ESP_BUILD) build'
	@echo "[DONE]"
	@echo ""

esp-flash:
	@echo "Flashing ESP32..."
	bash -c 'source "$(IDF_ACTIVATE)" && $(ESP_IDF) -C $(ESP_SRC) -B $(ESP_BUILD) -p $(PORT) flash'
	@echo "[DONE]"
	@echo ""