# Nezha Cyber (哪吒网络安全) Firmware Makefile
# Author: 钟智强 (ctkqiang)
# Auto-detects PlatformIO, connected serial port, chip flash capacity, and target board:
#   - /dev/cu.usbmodem1101         -> LilyGO T-Deck (lilygo-t-deck, ESP32-S3, 16MB)
#   - /dev/cu.usbserial-81526C1AF0 -> M5StickC Plus 1.1 (m5stack-cplus1_1, ESP32, 4MB <4MB)

SHELL := /bin/sh

TDECK_PORT  ?= /dev/cu.usbmodem1101
STICKC_PORT ?= /dev/cu.usbserial-81526C1AF0
REPO        ?= NezhaCyber/nezha_cyber_firmware
RELEASE     ?=
CHANNEL     ?=

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
BIN          ?= NezhaCyber-$(ENV).bin
OTA_BIN      ?= .pio/build/$(ENV)/firmware.bin
ERASE        ?= 0

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
			echo "   1. Connect via USB-C data cable and run 'make stickc-plus' (or 'make stickc-plus-lite')."; \
			exit 1; \
		fi; \
	fi
endef

.PHONY: help check-pio check-port check-size detect ports envs releases build install install-release install-beta \
        upload flash-bin monitor clean test \
        tdeck tdeck-plus cardputer stickc-plus cplus1_1 stickc-plus-lite stickc-plus2 core2 cyd

.DEFAULT_GOAL := help

help: ## Show this help message
	@echo ""
	@echo "🔥 Nezha Cyber (哪吒网络安全) Build & Flash Helper"
	@echo "=================================================="
	@echo "  ENV          = $(ENV)"
	@echo "  CHIP         = $(CHIP)"
	@echo "  PORT         = $(PORT)"
	@echo "  BAUD         = $(BAUD)"
	@echo "  REPO         = $(REPO)"
	@echo "  PIO          = $(if $(PIO),$(PIO),<not found>)"
	@echo ""
	@echo "Known auto-detected ports & flash tiers:"
	@echo "  $(TDECK_PORT)         -> lilygo-t-deck (ESP32-S3, 16MB Full)"
	@echo "  $(STICKC_PORT) -> m5stack-cplus1_1 (ESP32, 4MB Compact <4MB)"
	@echo ""
	@echo "Targets:"
	@grep -E '^[a-zA-Z0-9_-]+:.*?## ' $(MAKEFILE_LIST) | awk 'BEGIN {FS = ":.*?## "}; {printf "  \033[36m%-18s\033[0m %s\n", $$1, $$2}'
	@echo ""
	@echo "Examples:"
	@echo "  make releases                               # List GitHub Releases (Stable & Beta) + firmware memory sizes"
	@echo "  make install-release                        # Auto-detect chip/flash & install latest Stable from GitHub"
	@echo "  make install-beta ENV=m5stack-cplus1_1      # Install latest Beta (<4MB) on M5StickC Plus 1.1"
	@echo "  make install RELEASE=v2.1.0                 # Install a specific GitHub Release tag"
	@echo "  make install                                # Build from source, check flash fit & flash connected device"
	@echo "  make tdeck                                  # Build & install on LilyGO T-Deck (16MB)"
	@echo "  make stickc-plus                            # Build & install Compact (<4MB) on M5StickC Plus 1.1"
	@echo "  make stickc-plus-lite                       # Build & install Launcher Lite (<2.5MB) on M5StickC Plus 1.1"
	@echo "  make detect                                 # Probe connected chip type & exact Flash capacity (4/8/16MB)"
	@echo "  make check-size ENV=m5stack-cplus1_1        # Verify built binary fits target device flash limit"
	@echo ""

check-pio: ## Verify PlatformIO CLI is available
	@if [ -z "$(PIO)" ] || [ ! -x "$(PIO)" ]; then \
		echo "❌ PlatformIO CLI not found."; \
		echo "Install PlatformIO or use 'make install-release' to flash directly from GitHub Releases."; \
		exit 1; \
	fi
	@echo "✅ Using PlatformIO: $(PIO)"

check-port: ## Check if preferred or any USB serial port is connected
	@$(RESOLVE_PORT)

detect: ## Probe connected USB serial port, ESP chip type & Flash size (4/8/16MB)
	@set -e; \
	$(RESOLVE_PORT); \
	python3 tools/install_firmware.py --probe-port "$$ACTIVE_PORT"

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

releases: ## List GitHub Releases (Stable / Beta) with per-board memory sizes
	@python3 tools/install_firmware.py --list-releases --repo "$(REPO)" $(if $(CHANNEL),--channel "$(CHANNEL)",)

check-size: ## Verify NezhaCyber-<ENV>.bin fits the target device Flash & partition ceiling
	@python3 tools/install_firmware.py --check-bin "$(BIN)" --env "$(ENV)" $(if $(wildcard $(OTA_BIN)),--ota-bin "$(OTA_BIN)",)

build: check-pio ## Build firmware for ENV and verify Flash/partition fit
	@echo "🔨 Building Nezha Cyber (哪吒网络安全) firmware for $(ENV) (chip=$(CHIP))..."
	"$(PIO)" run -e "$(ENV)"
	@if [ -f "$(BIN)" ]; then \
		python3 tools/install_firmware.py --check-bin "$(BIN)" --env "$(ENV)" $(if $(wildcard $(OTA_BIN)),--ota-bin "$(OTA_BIN)",); \
	fi

install-release: ## Auto-detect chip/flash & install prebuilt firmware from GitHub Releases
	@python3 tools/install_firmware.py --install-release \
		--repo "$(REPO)" \
		--tag "$(if $(RELEASE),$(RELEASE),latest)" \
		$(if $(CHANNEL),--channel "$(CHANNEL)",--channel stable) \
		$(if $(filter-out undefined,$(origin ENV)),--env "$(ENV)",) \
		$(if $(filter-out undefined,$(origin PORT)),--port "$(PORT)",) \
		--baud "$(BAUD)" \
		$(if $(filter 1 true yes,$(ERASE)),--erase,)

install-beta: ## Auto-detect chip/flash & install latest Beta release from GitHub Releases
	@$(MAKE) install-release RELEASE=beta CHANNEL=beta

install: ## Build & flash from source (or pass RELEASE=vX.Y.Z / CHANNEL=beta to install from GitHub)
	@if [ -n "$(RELEASE)" ] || [ -n "$(CHANNEL)" ]; then \
		$(MAKE) install-release; \
	else \
		set -e; \
		$(MAKE) check-pio; \
		$(RESOLVE_PORT); \
		echo "🚀 Building $(ENV) (chip=$(CHIP))..."; \
		"$(PIO)" run -e "$(ENV)"; \
		if [ -f "$(BIN)" ]; then \
			python3 tools/install_firmware.py --check-bin "$(BIN)" --env "$(ENV)" $(if $(wildcard $(OTA_BIN)),--ota-bin "$(OTA_BIN)",); \
		fi; \
		echo "⚡ Flashing $(ENV) to $$ACTIVE_PORT..."; \
		"$(PIO)" run -e "$(ENV)" -t upload -t nobuild --upload-port "$$ACTIVE_PORT"; \
		echo ""; \
		echo "🎉 Flash complete! Press the Reset button (or power-cycle) on your device to boot Nezha Cyber."; \
	fi

upload: check-pio ## Flash already-built firmware without rebuilding (nobuild)
	@set -e; \
	if [ -f "$(BIN)" ]; then \
		python3 tools/install_firmware.py --check-bin "$(BIN)" --env "$(ENV)" $(if $(wildcard $(OTA_BIN)),--ota-bin "$(OTA_BIN)",); \
	fi; \
	$(RESOLVE_PORT); \
	echo "⚡ Uploading prebuilt $(ENV) firmware to $$ACTIVE_PORT..."; \
	"$(PIO)" run -e "$(ENV)" -t upload -t nobuild --upload-port "$$ACTIVE_PORT"; \
	echo ""; \
	echo "🎉 Upload complete! Press the Reset button (or power-cycle) on your device to boot Nezha Cyber."

flash-bin: ## Validate flash limit & flash merged NezhaCyber-<ENV>.bin directly at 0x00000
	@set -e; \
	if [ ! -f "$(BIN)" ]; then \
		echo "ℹ️  $(BIN) not found. Building first..."; \
		$(MAKE) build ENV="$(ENV)"; \
	fi; \
	python3 tools/install_firmware.py --check-bin "$(BIN)" --env "$(ENV)"; \
	$(RESOLVE_PORT); \
	echo "⚡ Flashing $(BIN) to $$ACTIVE_PORT (chip=$(CHIP), baud=$(BAUD))..."; \
	if [ -n "$(ESPTOOL)" ]; then \
		"$(ESPTOOL)" --chip "$(CHIP)" --port "$$ACTIVE_PORT" --baud "$(BAUD)" write-flash 0x00000 "$(BIN)"; \
	elif [ -n "$(PIO)" ]; then \
		"$(PIO)" pkg exec -p tool-esptoolpy -- esptool.py --chip "$(CHIP)" --port "$$ACTIVE_PORT" --baud "$(BAUD)" write-flash 0x00000 "$(BIN)"; \
	else \
		echo "❌ Neither esptool nor PlatformIO was found."; \
		exit 1; \
	fi; \
	echo ""; \
	echo "🎉 Flash complete! Press the Reset button (or power-cycle) on your device to boot Nezha Cyber."

monitor: check-pio ## Open serial monitor on the connected port
	@set -e; \
	$(RESOLVE_PORT); \
	echo "📟 Opening serial monitor on $$ACTIVE_PORT ($(MONITOR_BAUD) baud)... (Ctrl+C to exit)"; \
	"$(PIO)" device monitor --port "$$ACTIVE_PORT" --baud "$(MONITOR_BAUD)"

clean: check-pio ## Clean PlatformIO build artifacts for ENV
	"$(PIO)" run -e "$(ENV)" -t clean

test: ## Run host-native C++17 & Python unit test suite
	@bash test/run_tests.sh

# Convenience device shortcuts (also support RELEASE=latest or CHANNEL=beta!)
tdeck: ## Install Nezha Cyber (16MB Full) on standard LilyGO T-Deck
	@$(MAKE) install ENV=lilygo-t-deck PORT="$(TDECK_PORT)"

tdeck-plus: ## Install Nezha Cyber (16MB Full) on LilyGO T-Deck Plus
	@$(MAKE) install ENV=lilygo-t-deck-pro PORT="$(TDECK_PORT)"

cardputer: ## Install Nezha Cyber (8MB) on M5Stack Cardputer
	@$(MAKE) install ENV=m5stack-cardputer PORT="$(TDECK_PORT)"

stickc-plus: ## Install Nezha Cyber (4MB Compact <4MB) on M5StickC Plus 1.1
	@$(MAKE) install ENV=m5stack-cplus1_1 PORT="$(STICKC_PORT)"

cplus1_1: stickc-plus ## Alias for stickc-plus (M5StickC Plus 1.1)

stickc-plus-lite: ## Install Nezha Cyber Launcher Lite (<2.5MB) on M5StickC Plus 1.1
	@$(MAKE) install ENV=LAUNCHER_m5stack-cplus1_1 PORT="$(STICKC_PORT)"

stickc-plus2: ## Install Nezha Cyber (8MB) on M5StickC Plus2
	@$(MAKE) install ENV=m5stack-cplus2 PORT="$(STICKC_PORT)"

core2: ## Install Nezha Cyber (16MB Full) on M5Stack Core2
	@$(MAKE) install ENV=m5stack-core2 PORT="$(STICKC_PORT)"

cyd: ## Install Nezha Cyber (4MB Compact <4MB) on CYD-2432S028
	@$(MAKE) install ENV=CYD-2432S028 PORT="$(STICKC_PORT)"
