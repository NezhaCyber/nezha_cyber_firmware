# Bruce Firmware Makefile
# Auto-detects PlatformIO, connected serial port, and target board:
#   - /dev/cu.usbmodem1101         -> LilyGO T-Deck (lilygo-t-deck, ESP32-S3)
#   - /dev/cu.usbserial-81526C1AF0 -> M5StickC Plus 1.1 (m5stack-cplus1_1, ESP32)

SHELL := /bin/sh

TDECK_PORT  ?= /dev/cu.usbmodem1101
STICKC_PORT ?= /dev/cu.usbserial-81526C1AF0

# Auto-pick default ENV based on which known device port is currently plugged in
ifeq ($(origin ENV), undefined)
  ifneq ($(shell [ -e "$(TDECK_PORT)" ] && echo 1),)
    ENV := lilygo-t-deck
  else ifneq ($(shell [ -e "$(STICKC_PORT)" ] && echo 1),)
    ENV := m5stack-cplus1_1
  else
    ENV := lilygo-t-deck
  endif
endif

# Match default PORT to the selected ENV when PORT is not explicitly provided
ifeq ($(origin PORT), undefined)
  ifneq ($(filter m5stack-cplus1_1 LAUNCHER_m5stack-cplus1_1 m5stack-cplus2 m5stack-core2 m5stack-core4mb m5stack-core16mb CYD-% LAUNCHER_CYD-% elecrow-24B elecrow-28B elecrow-35B elecrow-35Bv2_2 esp-wroom32-%,$(ENV)),)
    PORT := $(STICKC_PORT)
  else
    PORT := $(TDECK_PORT)
  endif
endif

# Match esptool CHIP argument to ENV
ifeq ($(origin CHIP), undefined)
  ifneq ($(filter m5stack-cplus1_1 LAUNCHER_m5stack-cplus1_1 m5stack-cplus2 m5stack-core2 m5stack-core4mb m5stack-core16mb CYD-% LAUNCHER_CYD-% elecrow-24B elecrow-28B elecrow-35B elecrow-35Bv2_2 esp-wroom32-%,$(ENV)),)
    CHIP := esp32
  else ifneq ($(findstring -c5,$(ENV)),)
    CHIP := esp32c5
  else
    CHIP := esp32s3
  endif
endif

BAUD         ?= 921600
MONITOR_BAUD ?= 115200
BIN          ?= Bruce-$(ENV).bin

# Prefer PlatformIO's managed venv (avoids Homebrew Python 3.15 incompatibility with ESP32 platform)
PIO ?= $(shell \
	if [ -x "$(HOME)/.platformio/penv/bin/pio" ]; then \
		echo "$(HOME)/.platformio/penv/bin/pio"; \
	elif command -v pio >/dev/null 2>&1; then \
		command -v pio; \
	elif command -v platformio >/dev/null 2>&1; then \
		command -v platformio; \
	fi)

ESPTOOL ?= $(shell \
	if command -v esptool >/dev/null 2>&1; then \
		command -v esptool; \
	elif command -v esptool.py >/dev/null 2>&1; then \
		command -v esptool.py; \
	fi)

# Helper shell snippet: resolves ACTIVE_PORT starting from $(PORT)
define RESOLVE_PORT
	ACTIVE_PORT=""; \
	if [ -e "$(PORT)" ]; then \
		ACTIVE_PORT="$(PORT)"; \
		echo "✅ Detected device on preferred port: $$ACTIVE_PORT (ENV=$(ENV), CHIP=$(CHIP))"; \
	else \
		echo "⚠️  Preferred port $(PORT) not found. Scanning for USB serial devices..."; \
		FOUND_PORTS="$$(ls /dev/cu.usbmodem* /dev/cu.usbserial* /dev/cu.SLAB_USBtoUART* /dev/cu.wchusbserial* /dev/ttyACM* /dev/ttyUSB* 2>/dev/null || true)"; \
		COUNT="$$(printf '%s\n' "$$FOUND_PORTS" | sed '/^$$/d' | wc -l | tr -d ' ')"; \
		if [ "$$COUNT" -eq 1 ]; then \
			ACTIVE_PORT="$$(printf '%s\n' "$$FOUND_PORTS" | sed '/^$$/d')"; \
			echo "✅ Auto-selected available port: $$ACTIVE_PORT (ENV=$(ENV), CHIP=$(CHIP))"; \
		elif [ "$$COUNT" -gt 1 ]; then \
			echo "❌ Multiple serial ports detected:"; \
			printf '%s\n' "$$FOUND_PORTS" | sed 's/^/   - /'; \
			echo "Please specify one explicitly, e.g.: make install ENV=m5stack-cplus1_1 PORT=/dev/cu.usbserial-81526C1AF0"; \
			exit 1; \
		else \
			echo "❌ No USB serial device found (expected $(PORT))."; \
			echo ""; \
			echo "👉 LilyGO T-Deck Download Mode (/dev/cu.usbmodem1101):"; \
			echo "   1. Power OFF -> press & HOLD Trackball (GPIO0) -> Power ON -> release."; \
			echo "👉 M5StickC Plus 1.1 (/dev/cu.usbserial-81526C1AF0):"; \
			echo "   1. Connect via USB-C data cable and run 'make stickc-plus'."; \
			exit 1; \
		fi; \
	fi
endef

.PHONY: help check-pio check-port detect ports envs build install upload flash-bin monitor clean \
        tdeck tdeck-plus cardputer stickc-plus cplus1_1 stickc-plus-lite stickc-plus2 core2 cyd

.DEFAULT_GOAL := help

help: ## Show this help message
	@echo ""
	@echo "🦈 Bruce Firmware Build & Flash Helper"
	@echo "======================================"
	@echo "  ENV          = $(ENV)"
	@echo "  CHIP         = $(CHIP)"
	@echo "  PORT         = $(PORT)"
	@echo "  BAUD         = $(BAUD)"
	@echo "  PIO          = $(if $(PIO),$(PIO),<not found>)"
	@echo ""
	@echo "Known auto-detected ports:"
	@echo "  $(TDECK_PORT)         -> lilygo-t-deck (ESP32-S3)"
	@echo "  $(STICKC_PORT) -> m5stack-cplus1_1 (ESP32)"
	@echo ""
	@echo "Targets:"
	@grep -E '^[a-zA-Z0-9_-]+:.*?## ' $(MAKEFILE_LIST) | awk 'BEGIN {FS = ":.*?## "}; {printf "  \033[36m%-18s\033[0m %s\n", $$1, $$2}'
	@echo ""
	@echo "Examples:"
	@echo "  make install                                # Auto-detect T-Deck or M5StickC Plus 1.1 & flash"
	@echo "  make tdeck                                  # Install on LilyGO T-Deck (/dev/cu.usbmodem1101)"
	@echo "  make stickc-plus                            # Install on M5StickC Plus 1.1 (/dev/cu.usbserial-81526C1AF0)"
	@echo "  make stickc-plus-lite                       # Install M5Launcher Lite on M5StickC Plus 1.1"
	@echo "  make detect                                 # Probe connected port & ESP chip type"
	@echo "  make upload                                 # Re-flash without rebuilding"
	@echo "  make monitor                                # Open serial monitor (115200 baud)"
	@echo ""

check-pio: ## Verify PlatformIO CLI is available
	@if [ -z "$(PIO)" ] || [ ! -x "$(PIO)" ]; then \
		echo "❌ PlatformIO CLI not found."; \
		echo "Install PlatformIO or ensure ~/.platformio/penv/bin/pio exists."; \
		exit 1; \
	fi
	@echo "✅ Using PlatformIO: $(PIO)"

check-port: ## Check if preferred or any USB serial port is connected
	@$(RESOLVE_PORT)

detect: ## Probe connected USB serial port and detect ESP chip type via esptool
	@set -e; \
	$(RESOLVE_PORT); \
	if [ -n "$(ESPTOOL)" ]; then \
		"$(ESPTOOL)" --port "$$ACTIVE_PORT" chip-id; \
	elif [ -n "$(PIO)" ]; then \
		"$(PIO)" pkg exec -p tool-esptoolpy -- esptool.py --port "$$ACTIVE_PORT" chip-id; \
	else \
		echo "❌ Neither esptool nor PlatformIO was found."; \
		exit 1; \
	fi

ports: ## List all detected USB serial ports
	@echo "🔍 Scanning /dev/cu.* and /dev/tty* ..."
	@FOUND="$$(ls /dev/cu.usbmodem* /dev/cu.usbserial* /dev/cu.SLAB_USBtoUART* /dev/cu.wchusbserial* /dev/ttyACM* /dev/ttyUSB* 2>/dev/null || true)"; \
	if [ -n "$$FOUND" ]; then \
		printf '%s\n' "$$FOUND" | sed 's/^/  - /'; \
	else \
		echo "  (no USB serial devices found)"; \
	fi

envs: ## List all supported board environments in platformio.ini
	@echo "📋 Available environments in platformio.ini:"
	@awk '/^default_envs =/{flag=1; next} /^$$/{flag=0} flag{gsub(/^[ \t]*;?[ \t]*/, ""); sub(/[ \t]*;.*/, ""); if (length($$0)) print "  - " $$0}' platformio.ini

build: check-pio ## Build firmware for ENV (generates Bruce-<ENV>.bin)
	@echo "🔨 Building Bruce firmware for $(ENV) (chip=$(CHIP))..."
	"$(PIO)" run -e "$(ENV)"
	@if [ -f "$(BIN)" ]; then \
		echo "✅ Merged binary ready: $(BIN)"; \
	fi

install: check-pio ## Auto-detect port & board, build, and flash firmware
	@set -e; \
	$(RESOLVE_PORT); \
	echo "🚀 Building and flashing $(ENV) (chip=$(CHIP)) to $$ACTIVE_PORT..."; \
	"$(PIO)" run -e "$(ENV)" -t upload --upload-port "$$ACTIVE_PORT"; \
	echo ""; \
	echo "🎉 Flash complete! Press the Reset button (or power-cycle) on your device to boot Bruce."

upload: check-pio ## Flash already-built firmware without rebuilding (nobuild)
	@set -e; \
	$(RESOLVE_PORT); \
	echo "⚡ Uploading prebuilt $(ENV) firmware to $$ACTIVE_PORT..."; \
	"$(PIO)" run -e "$(ENV)" -t upload -t nobuild --upload-port "$$ACTIVE_PORT"; \
	echo ""; \
	echo "🎉 Upload complete! Press the Reset button (or power-cycle) on your device to boot Bruce."

flash-bin: ## Flash merged Bruce-<ENV>.bin directly via esptool at 0x00000
	@set -e; \
	if [ ! -f "$(BIN)" ]; then \
		echo "ℹ️  $(BIN) not found. Building first..."; \
		$(MAKE) build ENV="$(ENV)"; \
	fi; \
	$(RESOLVE_PORT); \
	echo "⚡ Flashing $(BIN) to $$ACTIVE_PORT (chip=$(CHIP), baud=$(BAUD))..."; \
	if [ -n "$(ESPTOOL)" ]; then \
		"$(ESPTOOL)" --chip "$(CHIP)" --port "$$ACTIVE_PORT" --baud "$(BAUD)" write_flash 0x00000 "$(BIN)"; \
	elif [ -n "$(PIO)" ]; then \
		"$(PIO)" pkg exec -p tool-esptoolpy -- esptool.py --chip "$(CHIP)" --port "$$ACTIVE_PORT" --baud "$(BAUD)" write_flash 0x00000 "$(BIN)"; \
	else \
		echo "❌ Neither esptool nor PlatformIO was found."; \
		exit 1; \
	fi; \
	echo ""; \
	echo "🎉 Flash complete! Press the Reset button (or power-cycle) on your device to boot Bruce."

monitor: check-pio ## Open serial monitor on the connected port
	@set -e; \
	$(RESOLVE_PORT); \
	echo "📟 Opening serial monitor on $$ACTIVE_PORT ($(MONITOR_BAUD) baud)... (Ctrl+C to exit)"; \
	"$(PIO)" device monitor --port "$$ACTIVE_PORT" --baud "$(MONITOR_BAUD)"

clean: check-pio ## Clean PlatformIO build artifacts for ENV
	"$(PIO)" run -e "$(ENV)" -t clean

# Convenience device shortcuts
tdeck: ## Auto-install Bruce on standard LilyGO T-Deck (ESP32-S3)
	@$(MAKE) install ENV=lilygo-t-deck PORT="$(TDECK_PORT)"

tdeck-plus: ## Auto-install Bruce on LilyGO T-Deck Plus (ESP32-S3)
	@$(MAKE) install ENV=lilygo-t-deck-pro PORT="$(TDECK_PORT)"

cardputer: ## Auto-install Bruce on M5Stack Cardputer (ESP32-S3)
	@$(MAKE) install ENV=m5stack-cardputer PORT="$(TDECK_PORT)"

stickc-plus: ## Auto-install Bruce on M5StickC Plus 1.1 (ESP32)
	@$(MAKE) install ENV=m5stack-cplus1_1 PORT="$(STICKC_PORT)"

cplus1_1: stickc-plus ## Alias for stickc-plus (M5StickC Plus 1.1)

stickc-plus-lite: ## Auto-install M5Launcher Lite on M5StickC Plus 1.1 (ESP32)
	@$(MAKE) install ENV=LAUNCHER_m5stack-cplus1_1 PORT="$(STICKC_PORT)"

stickc-plus2: ## Auto-install Bruce on M5StickC Plus2 (ESP32)
	@$(MAKE) install ENV=m5stack-cplus2 PORT="$(STICKC_PORT)"

core2: ## Auto-install Bruce on M5Stack Core2 (ESP32)
	@$(MAKE) install ENV=m5stack-core2 PORT="$(STICKC_PORT)"

cyd: ## Auto-install Bruce on CYD-2432S028 (ESP32)
	@$(MAKE) install ENV=CYD-2432S028 PORT="$(STICKC_PORT)"
