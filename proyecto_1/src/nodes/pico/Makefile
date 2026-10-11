# --------------------------------------------------
# Raspberry Pi Pico SDK Build System
# --------------------------------------------------

BUILD_DIR = build

.PHONY: all pico pico_w clean clean_pico clean_pico_w

# Build both boards
all: pico pico_w

# Build Raspberry Pi Pico
pico:
	cmake -S . -B $(BUILD_DIR)/pico -G "Unix Makefiles" \
		-DPICO_BOARD=pico
	cmake --build $(BUILD_DIR)/pico

# Build Raspberry Pi Pico W
pico_w:
	cmake -S . -B $(BUILD_DIR)/pico_w -G "Unix Makefiles" \
		-DPICO_BOARD=pico_w
	cmake --build $(BUILD_DIR)/pico_w

# Clean both builds
clean:
	rm -rf $(BUILD_DIR)

# Clean individual builds
clean_pico:
	rm -rf $(BUILD_DIR)/pico

clean_pico_w:
	rm -rf $(BUILD_DIR)/pico_w
