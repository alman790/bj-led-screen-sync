APP_NAME := BJLEDAmbilight
APP_BUNDLE := build/$(APP_NAME).app
APP_BIN := $(APP_BUNDLE)/Contents/MacOS/$(APP_NAME)
APP_PLIST := $(APP_BUNDLE)/Contents/Info.plist
SRC := src/main.mm src/macos/AppDelegate.mm src/macos/DisplayCapture.mm src/macos/LedBluetooth.mm
VERSION := $(shell cat VERSION.txt 2>/dev/null || echo 0.1.0)
DIST_DIR := dist
CORE_SMOKE := build/core_smoke
INSTALL_DIR := $(HOME)/Applications
INSTALLED_APP := $(INSTALL_DIR)/$(APP_NAME).app
SIGN_IDENTITY := BJ LED Ambilight Local

CXX := clang++
CXXFLAGS := -std=c++20 -O3 -flto -Isrc -fobjc-arc -Wall -Wextra -Wpedantic
CORE_CXXFLAGS := -std=c++20 -O3 -flto -Isrc -Wall -Wextra -Wpedantic
LDFLAGS := -framework Cocoa -framework CoreBluetooth -framework CoreGraphics -framework ApplicationServices

.PHONY: all app signing-identity install-local run dev-run run-installed reset-permissions test lint format dist-macos dist-macos-installers dist-source release release-manifest installers clean linux windows

all: app

app: signing-identity $(APP_BIN)

signing-identity:
	@if [ "$(SIGN_IDENTITY)" = "-" ]; then \
		echo "Using ad-hoc signing"; \
	else \
		chmod +x scripts/create-local-codesign-identity.sh; \
		scripts/create-local-codesign-identity.sh "$(SIGN_IDENTITY)"; \
	fi

$(APP_BIN): $(SRC) src/lib/macos/AppDelegate.hpp src/lib/macos/DisplayCapture.hpp src/lib/macos/LedBluetooth.hpp src/lib/bj_core.hpp src/resources/Info.plist src/resources/AppIcon.icns src/resources/icons/app-icon.png
	rm -rf "$(APP_BUNDLE)"
	mkdir -p "$(APP_BUNDLE)/Contents/MacOS" "$(APP_BUNDLE)/Contents/Resources"
	cp src/resources/Info.plist "$(APP_PLIST)"
	cp src/resources/AppIcon.icns "$(APP_BUNDLE)/Contents/Resources/AppIcon.icns"
	cp src/resources/icons/app-icon.png "$(APP_BUNDLE)/Contents/Resources/app-icon.png"
	$(CXX) $(CXXFLAGS) $(SRC) $(LDFLAGS) -o "$(APP_BIN)"
	find "$(APP_BUNDLE)" -print -exec xattr -c {} \; 2>/dev/null || true
	xattr -cr "$(APP_BUNDLE)" || true
	xattr -dr com.apple.FinderInfo "$(APP_BUNDLE)" 2>/dev/null || true
	xattr -dr com.apple.fileprovider.fpfs#P "$(APP_BUNDLE)" 2>/dev/null || true
	xattr -dr com.apple.provenance "$(APP_BUNDLE)" 2>/dev/null || true
	codesign --force --deep --sign "$(SIGN_IDENTITY)" "$(APP_BUNDLE)"

install-local: app
	mkdir -p "$(INSTALL_DIR)"
	rm -rf "$(INSTALLED_APP)"
	ditto "$(APP_BUNDLE)" "$(INSTALLED_APP)"
	xattr -cr "$(INSTALLED_APP)"
	codesign --force --deep --sign "$(SIGN_IDENTITY)" "$(INSTALLED_APP)"

$(CORE_SMOKE): src/core_smoke.cpp src/lib/bj_core.hpp
	mkdir -p build
	$(CXX) $(CORE_CXXFLAGS) src/core_smoke.cpp -o "$(CORE_SMOKE)"

test: $(CORE_SMOKE)
	"$(CORE_SMOKE)"

lint:
	git diff --check
	python3 -m json.tool releases/manifest.json >/dev/null
	bash -n scripts/create-local-codesign-identity.sh scripts/generate-release-manifest.sh scripts/install.sh scripts/package-linux.sh scripts/package-macos.sh

format:
	@if command -v clang-format >/dev/null 2>&1; then \
		find src -type f \( -name '*.cpp' -o -name '*.hpp' -o -name '*.mm' \) -print0 | xargs -0 clang-format -i; \
	else \
		echo "clang-format is not installed"; exit 1; \
	fi

run:
	@if [ ! -d "$(INSTALLED_APP)" ]; then \
		$(MAKE) install-local; \
	fi
	open -n "$(INSTALLED_APP)"

dev-run: install-local
	open -n "$(INSTALLED_APP)"

run-installed:
	open -n "$(INSTALLED_APP)"

reset-permissions:
	tccutil reset ScreenCapture local.bj-led.ambilight || true

dist-macos: app test
	mkdir -p "$(DIST_DIR)"
	ditto -c -k --keepParent "$(APP_BUNDLE)" "$(DIST_DIR)/$(APP_NAME)-$(VERSION)-macos.zip"

dist-macos-installers:
	chmod +x scripts/package-macos.sh
	scripts/package-macos.sh

dist-source:
	mkdir -p "$(DIST_DIR)"
	rm -f "$(DIST_DIR)/$(APP_NAME)-$(VERSION)-source.zip"
	zip -qr "$(DIST_DIR)/$(APP_NAME)-$(VERSION)-source.zip" . -x ".git/*" ".venv/*" "build/*" "dist/*" "__pycache__/*" "*.png"

release: dist-macos dist-source
	@echo "macOS artifact: $(DIST_DIR)/$(APP_NAME)-$(VERSION)-macos.zip"
	@echo "Source artifact: $(DIST_DIR)/$(APP_NAME)-$(VERSION)-source.zip"
	@echo "Build Linux on Linux with: make linux"
	@echo "Build Windows on Windows with: nmake /f Makefile.windows"

release-manifest:
	chmod +x scripts/generate-release-manifest.sh
	scripts/generate-release-manifest.sh

installers: dist-macos-installers
	@echo "Linux installer: run scripts/package-linux.sh on Linux"
	@echo "Windows installer: run powershell -ExecutionPolicy Bypass -File scripts/package-windows.ps1 on Windows"

linux:
	@echo "Linux release source is in src/platform/linux. Build on Linux with: make -f Makefile.linux"

windows:
	@echo "Windows release source is in src/platform/windows. Build on Windows with: nmake /f Makefile.windows"

clean:
	rm -rf build "$(DIST_DIR)"
