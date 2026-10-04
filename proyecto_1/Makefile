# Tools
CC = gcc

# Flags
CFLAGS := -Wall -Wextra -std=c17 -MMD -MP -Iinclude
OPTS   ?= -O0

# Directories
SRC   := src
OUT   := output
BUILD := build

# Targets
TARGET ?= main

# Files
SOURCE := $(shell find ${SRC} -name '*.c')
OBJS   := $(patsubst ${SRC}/%.c,${BUILD}/%.o,${SOURCE})
DEPS   := $(OBJS:.o=.d)


# ---------------------------------------------------------------
.PHONY: all clean 

all: run

run:
	@echo "Running program:$(OUT)/$(TARGET)..."
	./$(OUT)/$(TARGET)
	@echo "[SUCCESS]"

# [RULE: make necessary directories]
dirs:
	@echo "Creating $(OUT)/ and $(BUILD)/ directories..."
	mkdir -p $(OUT) $(BUILD)
	@echo "[COMPLETED]"

# [RULE: compile dev build]
build: dirs $(OBJS)
	@echo ""
	@echo "Compiling program:$(OUT)/$(TARGET)..."
	$(CC) $(CFLAGS) \
	-o $(OUT)/$(TARGET) \
	$(OBJS)
	@echo "[COMPLETED]"
	@echo ""

# [RULE: compile debug build]
debug: dirs $(OBJS)
	@echo ""
	@echo "Compiling program:$(OUT)/$(TARGET) in debug mode..."
	$(CC) $(CFLAGS) -g \
	-o $(OUT)/$(TARGET) \
	$(OBJS)
	@echo "[COMPLETED]"
	@echo ""

# [RULE: generic rule]
$(BUILD)/%.o: $(SRC)/%.c
	@echo "> Compiling program dependency: $@..."
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(OPTS) -c $< -o $@
	@echo "[DONE]"

# [RULE: clean output directories]
clean:
	@echo "Cleaning $(OUT)/ and $(BUILD)/ directories..."
	rm -rf $(OUT) $(BUILD)
	@echo "[COMPLETED]"

-include $(DEPS)