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
          -std=c11 \
          -D_POSIX_C_SOURCE=200809L \
          -MMD -MP \
          -Iinclude

ifeq ($(MODE),release)
    CFLAGS += -O2
else
    CFLAGS += -O0 -g3
endif

# =============================================================================
# Linker
# =============================================================================
LDLIBS := -pthread -lm -lcrypto

# =============================================================================
# Makefile variables
# =============================================================================
DEV ?= USB0

IDF_ACTIVATE ?= $(HOME)/.espressif/tools/activate_idf_v6.1.sh

PICO_SDK_PATH ?= $(HOME)/pico-sdk
export PICO_SDK_PATH

# =============================================================================
# Directories
# =============================================================================
SRC_DIR := src
OUT_DIR := output
BUILD_DIR := build

COMMON_SRC := $(SRC_DIR)/common

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
    $(COMMON_SRC) \
	$(SRC_DIR)/serial)

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
	$(SRC_DIR)/serial)

CLIENT_SOURCES := $(foreach dir,$(CLIENT_SRC_DIRS),\
    $(shell find $(dir) -type f -name '*.c'))

CLIENT_OBJS := $(patsubst \
    $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(CLIENT_SOURCES))

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
	bash -c 'source "$(IDF_ACTIVATE)" && $(ESP_IDF) -C $(ESP_SRC) -B $(ESP_BUILD) -p /dev/tty$(DEV) flash'
	@echo "[DONE]"
	@echo ""

# ==================================
# Encrypted TCP entry points in the existing client/server programs
# ==================================
TCP_PORT ?= 5000
TCP_HOST ?= 127.0.0.1
IMAGE ?= images/fgm-scaling-heat.png
TCP_CLIENT_BIN := $(CLIENT_OUT)
TCP_SERVER_BIN := $(SERVER_OUT)

# Pass paths as environment values, preserving spaces and shell metacharacters.
export DIPS_TCP_IMAGE = $(IMAGE)
export DIPS_TCP_PORT = $(TCP_PORT)
export DIPS_TCP_HOST = $(TCP_HOST)

.PHONY: tcp-build tcp-server tcp-client tcp-demo

tcp-build: client-build server-build

# For separate terminals/machines, explicitly supply the same DIPS_AES_KEY.
tcp-server: server-build
	@"$(TCP_SERVER_BIN)" "$$DIPS_TCP_PORT"

tcp-client: client-build
	@"$(TCP_CLIENT_BIN)" "$$DIPS_TCP_HOST" "$$DIPS_TCP_PORT" "$$DIPS_TCP_IMAGE"

# One-terminal local demo. Bash coproc waits for the server's startup message;
# no fixed startup delay, temporary key file, or extra readiness connection.
# Example: make tcp-demo IMAGE="/ruta/otra imagen.png" TCP_PORT=5001
tcp-demo: tcp-build
	@bash -ec '\
		if [[ ! -f "$$DIPS_TCP_IMAGE" || ! -r "$$DIPS_TCP_IMAGE" ]]; then \
			printf "Imagen inexistente o no legible: %s\n" "$$DIPS_TCP_IMAGE" >&2; \
			exit 1; \
		fi; \
		if [[ ! -v DIPS_AES_KEY ]]; then \
			DIPS_AES_KEY="$$(openssl rand -hex 32)"; \
			export DIPS_AES_KEY; \
		fi; \
		coproc DIPS_SERVER { exec "$(TCP_SERVER_BIN)" "$$DIPS_TCP_PORT"; }; \
		server_pid=$${DIPS_SERVER_PID:-}; \
		server_output=$${DIPS_SERVER[0]:-}; \
		if [[ -z "$$server_pid" ]]; then exit 1; fi; \
		trap '\''kill "$$server_pid" 2>/dev/null || :; wait "$$server_pid" 2>/dev/null || :'\'' EXIT; \
		trap '\''exit 130'\'' INT; \
		trap '\''exit 143'\'' TERM; \
		if [[ -z "$$server_output" ]] || ! IFS= read -r ready <&"$$server_output"; then \
			printf "No se pudo iniciar el servidor TCP.\n" >&2; \
			exit 1; \
		fi; \
		printf "%s\n" "$$ready"; \
		"$(TCP_CLIENT_BIN)" 127.0.0.1 "$$DIPS_TCP_PORT" "$$DIPS_TCP_IMAGE"; \
		IFS= read -r received <&"$$server_output"; \
		printf "%s\n" "$$received"; \
	'
