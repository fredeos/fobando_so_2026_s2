# =============================================================================
# Makefile tools and build mode
# =============================================================================
CC := gcc
MPICC ?= mpicc
MPIRUN ?= mpirun
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
LDLIBS := -pthread -lm -lcrypto

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
    $(COMMON_SRC) \
	$(SRC_DIR)/distributed \
	$(SRC_DIR)/image-filters \
	$(SRC_DIR)/serial \
	$(3PARTY_SRC))

SERVER_SOURCES := $(foreach dir,$(SERVER_SRC_DIRS),\
    $(shell find $(dir) -type f -name '*.c'))
SERVER_SOURCES := $(filter-out $(SRC_DIR)/distributed/main.c,$(SERVER_SOURCES))

SERVER_OBJS := $(patsubst \
    $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(filter $(SRC_DIR)/%.c,$(SERVER_SOURCES))) \
    $(patsubst \
    $(3PARTY_SRC)/%.c,$(BUILD_DIR)/$(3PARTY_SRC)/%.o,$(filter $(3PARTY_SRC)/%.c,$(SERVER_SOURCES)))

SERVER_DEPS := $(sort \
    $(SERVER_OBJS:.o=.d))

# << Client sources, objects and dependencies >>

# Compilar únicamente fuente y módulos comunes
CLIENT_SRC_DIRS := $(wildcard \
    $(CLIENT_SRC) \
    $(COMMON_SRC) \
	$(SRC_DIR)/image-filters \
	$(SRC_DIR)/serial \
	$(3PARTY_SRC))

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

# =============================================================================
# Static screen library
# =============================================================================
SCREEN_LIB_SRC := $(SRC_DIR)/screen
SCREEN_LIB_BUILD := $(BUILD_DIR)/screen
SCREEN_LIB_OUT := $(OUT_DIR)/libscreen.a
SCREEN_LIB_SOURCES := $(shell find $(SCREEN_LIB_SRC) -type f -name '*.c')
SCREEN_LIB_OBJECTS := $(patsubst $(SCREEN_LIB_SRC)/%.c,$(SCREEN_LIB_BUILD)/%.o,$(SCREEN_LIB_SOURCES))

$(SCREEN_LIB_BUILD)/%.o: $(SCREEN_LIB_SRC)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

.PHONY: screen-library
screen-library: $(SCREEN_LIB_OBJECTS)
	@mkdir -p $(dir $(SCREEN_LIB_OUT))
	ar rcs $(SCREEN_LIB_OUT) $(SCREEN_LIB_OBJECTS)

# Host-side screen device driver, linked with the existing POSIX serial and
# framing protocol implementations. The ESP32 firmware is a separate target.
SCREEN_DRIVER_OUT := $(OUT_DIR)/libscreen_driver.a
SCREEN_DRIVER_OBJECTS := $(BUILD_DIR)/driver/screen_driver.o \
	$(BUILD_DIR)/serial/serial.o \
	$(BUILD_DIR)/common/protocol/protocol_misc.o \
	$(BUILD_DIR)/common/protocol/protocol_tx.o \
	$(BUILD_DIR)/common/protocol/protocol_rx.o \
	$(BUILD_DIR)/common/conversion.o

.PHONY: screen-driver-library
screen-driver-library: $(SCREEN_DRIVER_OBJECTS)
	@mkdir -p $(dir $(SCREEN_DRIVER_OUT))
	ar rcs $(SCREEN_DRIVER_OUT) $(SCREEN_DRIVER_OBJECTS)

SCREEN_DRIVER_DEMO := $(OUT_DIR)/screen-driver-demo
SCREEN_DRIVER_DEMO_OBJECT := $(BUILD_DIR)/driver/screen_driver_demo.o

.PHONY: screen-driver-demo
screen-driver-demo: screen-driver-library $(SCREEN_DRIVER_DEMO_OBJECT)
	$(CC) $(CFLAGS) $(SCREEN_DRIVER_DEMO_OBJECT) -L$(OUT_DIR) -lscreen_driver -o $(SCREEN_DRIVER_DEMO)

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

# Server entry points and MPI modules need OpenMPI's compiler/include paths.
$(SERVER_OBJS): CC := $(MPICC)
$(SCREEN_DRIVER_OBJECTS): CC := gcc

$(BUILD_DIR)/distributed/%.o: $(SRC_DIR)/distributed/%.c
	@mkdir -p $(dir $@)
	$(MPICC) $(CFLAGS) -c $< -o $@

# =============================================================================
# Makefile server rules and variables
# =============================================================================
.PHONY: server-build server-debug server-release

server-build: $(SERVER_OBJS)
	$(MAKE) dirs
	@mkdir -p $(dir ${SERVER_OUT}) $(dir ${SERVER_BUILD})
	$(MPICC) $(CFLAGS) $^ -o $(SERVER_OUT) $(LDLIBS) -lpng -ljpeg

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
-include $(SCREEN_DRIVER_OBJECTS:.o=.d)

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

# ==================================
# Encrypted TCP entry points in the existing client/server programs
# ==================================
TCP_PORT ?= 5000
TCP_HOST ?= 127.0.0.1
IMAGE ?= images/images.jpeg
TCP_CLIENT_BIN := $(CLIENT_OUT)
TCP_SERVER_BIN := $(SERVER_OUT)

# Pass paths as environment values, preserving spaces and shell metacharacters.
export DIPS_TCP_IMAGE = $(IMAGE)
export DIPS_TCP_PORT = $(TCP_PORT)
export DIPS_TCP_HOST = $(TCP_HOST)

.PHONY: tcp-build tcp-server tcp-client tcp-demo

tcp-build: client-build server-build

# Keep the original commands as aliases to the integrated pipeline.
tcp-server: cluster-server
tcp-client: cluster-client
tcp-demo: cluster-demo

# TCP -> parallel resize -> parallel filter -> authenticated PNG response.
MPI_NP ?= 4
FILTER ?= blur
# RESIZE=original preserves dimensions and skips the parallel resize stage.
RESIZE ?= 128x160
BLOCK_ROWS ?= 16
RESULT ?= $(OUT_DIR)/processed.png
CLUSTER_HOSTS ?=

export DIPS_CLUSTER_NP = $(MPI_NP)
export DIPS_CLUSTER_HOSTS = $(CLUSTER_HOSTS)
export DIPS_CLUSTER_FILTER = $(FILTER)
export DIPS_CLUSTER_RESIZE = $(RESIZE)
export DIPS_CLUSTER_ROWS = $(BLOCK_ROWS)
export DIPS_CLUSTER_RESULT = $(RESULT)
export DIPS_CLUSTER_MPIRUN = $(MPIRUN)
export DIPS_CLUSTER_SERVER = $(abspath $(SERVER_OUT))
export DIPS_CLUSTER_CLIENT = $(abspath $(CLIENT_OUT))

.PHONY: cluster-build cluster-demo cluster-server cluster-client distributed-build
cluster-build: client-build server-build

cluster-demo: cluster-build
	@bash scripts/cluster-demo.sh

cluster-server: server-build
	@$(MPIRUN) --oversubscribe -np "$$DIPS_CLUSTER_NP" --map-by slot \
		$${DIPS_CLUSTER_HOSTS:+--host "$$DIPS_CLUSTER_HOSTS"} -x DIPS_AES_KEY \
		"$(abspath $(SERVER_OUT))" "$$DIPS_TCP_PORT"

cluster-client: client-build
	@"$(CLIENT_OUT)" "$$DIPS_TCP_HOST" "$$DIPS_TCP_PORT" "$$DIPS_TCP_IMAGE" \
		--filter "$$DIPS_CLUSTER_FILTER" --resize "$$DIPS_CLUSTER_RESIZE" \
		--block-rows "$$DIPS_CLUSTER_ROWS" --output "$$DIPS_CLUSTER_RESULT"

DISTRIBUTED_OBJS := $(BUILD_DIR)/distributed/main.o $(BUILD_DIR)/distributed/distributed.o \
    $(BUILD_DIR)/distributed/mpi_transport.o $(BUILD_DIR)/image-filters/filters.o \
    $(BUILD_DIR)/common/crypto.o $(BUILD_DIR)/common/image_job.o
-include $(DISTRIBUTED_OBJS:.o=.d)
distributed-build: $(OUT_DIR)/distributed
$(OUT_DIR)/distributed: $(DISTRIBUTED_OBJS)
	@mkdir -p "$(OUT_DIR)"
	$(MPICC) $(CFLAGS) $^ -o "$@" -lcrypto -lm

# Preserve the JSON serializer/parser demonstration from p2.
.PHONY: json-demo
json-demo: client-build
	@"$(CLIENT_OUT)" --json-demo
