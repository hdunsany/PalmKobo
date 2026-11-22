# palmkobo - Palm OS (CloudpilotEmu) front end for Kobo e-readers
# SPDX-License-Identifier: GPL-3.0-or-later
#
#   make                      host build (desktop Linux, no e-ink; for development/tests)
#   make TARGET=kobo          cross-compile for Kobo (needs koxtoolchain + FBInk)
#   make test                 run unit tests (host)
#   make TARGET=kobo package  build the KoboRoot-free install zip (see README)
#
# Paths you will probably want to set:
#   CLOUDPILOT=/path/to/cloudpilot   (git clone --recurse-submodules of cloudpilot-emu/cloudpilot)
#   FBINK=/path/to/FBInk             (git clone --recursive of NiLuJe/FBInk; kobo only)
#   CROSS_TC=arm-kobo-linux-gnueabihf (koxtoolchain triplet; kobo only)

TARGET ?= host
CLOUDPILOT ?= ../cloudpilot
FBINK ?= ../FBInk
CROSS_TC ?= arm-kobo-linux-gnueabihf
JOBS ?= $(shell nproc 2>/dev/null || echo 4)

ROOT := $(abspath .)
BUILD := $(ROOT)/build/$(TARGET)
MKCORE := $(ROOT)/mk/core.mk
CP_SRC := $(abspath $(CLOUDPILOT))/src
CP_EMU := $(CP_SRC)/cloudpilot
CP_COMMON := $(CP_SRC)/common
CP_VFS := $(CP_SRC)/vfs
CP_FSTOOLS := $(CP_SRC)/fstools

ifeq ($(TARGET),kobo)
  CC := $(CROSS_TC)-gcc
  CXX := $(CROSS_TC)-g++
  AR := $(CROSS_TC)-gcc-ar
  RANLIB := $(CROSS_TC)-gcc-ranlib
  STRIP := $(CROSS_TC)-strip
  OPT := -O2 -g0
  FBINK_LIB := $(abspath $(FBINK))/Release/libfbink.a
  FRONTEND_DEFS := -DPK_WITH_FBINK -DFBINK_FOR_KOBO -I$(abspath $(FBINK))
  FRONTEND_LIBS := $(FBINK_LIB) -lm -lrt
  LDFLAGS_EXTRA := -static-libstdc++ -static-libgcc
else ifeq ($(TARGET),host)
  CC := gcc
  CXX := g++
  AR := ar
  RANLIB := ranlib
  STRIP := strip
  OPT := -O2 -g
  FBINK_LIB :=
  FRONTEND_DEFS :=
  FRONTEND_LIBS :=
  LDFLAGS_EXTRA :=
else
  $(error TARGET must be host or kobo)
endif

# Arguments handed to CloudpilotEmu's own Makefiles so their sources build with our
# compiler, without SDL, into a per-target build directory inside their tree.
CORE_MAKE_ARGS = -f Makefile -f $(MKCORE) \
	CC_NATIVE="$(CC)" CXX_NATIVE="$(CXX)" LD_NATIVE="$(CXX)" \
	AR_NATIVE="$(AR)" RANLIB_NATIVE="$(RANLIB)" \
	CFLAGS_NATIVE="$(OPT)" \
	BUILDDIR_NATIVE=.build-palmkobo-$(TARGET) DEPDIR_NATIVE=.deps-palmkobo-$(TARGET)

CORE_LIB := $(BUILD)/libcpcore.a
COMMON_LIB := $(BUILD)/libcpcommon.a
VFS_LIB := $(BUILD)/libcpvfs.a
FSTOOLS_LIB := $(BUILD)/libcpfstools.a
CARD_INCLUDE := -I$(CP_COMMON) -I$(CP_VFS) -I$(CP_FSTOOLS)

# Include path of the emulator core, as declared by its Makefile (relative to its dir).
CP_INCLUDE_REL := $(shell $(MAKE) -s --no-print-directory -C $(CP_EMU) -f Makefile -f $(MKCORE) palmkobo-print-include 2>/dev/null)
CP_INCLUDE := $(patsubst -I%,-I$(CP_EMU)/%,$(CP_INCLUDE_REL)) -I$(CP_EMU)/native

CXXFLAGS := -std=c++17 $(OPT) -Wall -Wextra -Wno-unused-parameter -Wno-multichar -Wno-unknown-pragmas \
	-DEMULATION_LEVEL=EMULATION_UNIX
LDLIBS := -lpthread -ldl

SRC := src/main.cpp src/App.cpp src/ButtonBar.cpp src/Config.cpp src/Input.cpp src/Keyboard.cpp src/CardFs.cpp \
	src/DisplayFBInk.cpp src/DisplayHost.cpp
OBJ := $(SRC:src/%.cpp=$(BUILD)/obj/%.o)

all: check-cloudpilot $(BUILD)/palmkobo

check-cloudpilot:
	@test -f $(CP_EMU)/Makefile || { echo "CloudpilotEmu not found at $(CLOUDPILOT); set CLOUDPILOT=..."; exit 1; }
ifeq ($(TARGET),kobo)
	@test -f $(FBINK_LIB) || { echo "FBInk static lib not found at $(FBINK_LIB)"; \
		echo "build it with: make -C $(FBINK) staticlib KOBO=true CROSS_TC=$(CROSS_TC)"; exit 1; }
endif

$(CORE_LIB): FORCE
	@mkdir -p $(BUILD)
	$(MAKE) -C $(CP_EMU) $(CORE_MAKE_ARGS) -j$(JOBS) palmkobo-core PALMKOBO_CORE_LIB=$@

$(COMMON_LIB): FORCE
	@mkdir -p $(BUILD)
	$(MAKE) -C $(CP_COMMON) $(CORE_MAKE_ARGS) -j$(JOBS) palmkobo-lib PALMKOBO_LIB=$@

$(VFS_LIB): FORCE
	@mkdir -p $(BUILD)
	$(MAKE) -C $(CP_VFS) $(CORE_MAKE_ARGS) -j$(JOBS) palmkobo-lib PALMKOBO_LIB=$@

$(FSTOOLS_LIB): FORCE
	@mkdir -p $(BUILD)
	$(MAKE) -C $(CP_FSTOOLS) $(CORE_MAKE_ARGS) -j$(JOBS) palmkobo-lib PALMKOBO_LIB=$@ \
		PALMKOBO_EXTRA_CXX=native/terminate.cpp CFLAGS_NATIVE="$(OPT) -D_GNU_SOURCE"

$(BUILD)/obj/%.o: src/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(FRONTEND_DEFS) $(CP_INCLUDE) $(CARD_INCLUDE) -MMD -MP -c -o $@ $<

$(BUILD)/palmkobo: $(OBJ) $(CORE_LIB) $(VFS_LIB) $(FSTOOLS_LIB) $(COMMON_LIB)
	$(CXX) -o $@ $(OBJ) $(CORE_LIB) $(VFS_LIB) $(FSTOOLS_LIB) $(COMMON_LIB) $(FRONTEND_LIBS) $(LDLIBS) $(LDFLAGS_EXTRA)
ifeq ($(TARGET),kobo)
	$(STRIP) $@
endif

# --- tests (host only, no emulator needed) ---------------------------------
TEST_BIN := build/test/unit_tests
$(TEST_BIN): test/unit_tests.cpp src/ButtonBar.cpp src/Config.cpp src/Input.cpp src/Keyboard.cpp src/*.h
	@mkdir -p $(dir $@)
	g++ -std=c++17 -O1 -g -Wall -Wextra -fsanitize=address,undefined -Isrc -o $@ \
		test/unit_tests.cpp src/ButtonBar.cpp src/Config.cpp src/Input.cpp src/Keyboard.cpp

test: $(TEST_BIN)
	./$(TEST_BIN)

# --- SD card tests (host only; needs the CloudpilotEmu card libraries) -------
CARD_TEST_BIN := build/test/card_tests
$(CARD_TEST_BIN): test/card_tests.cpp src/CardFs.cpp src/CardFs.h $(VFS_LIB) $(FSTOOLS_LIB) $(COMMON_LIB)
	@mkdir -p $(dir $@)
	$(CXX) -std=c++17 -O1 -g -Wall -Isrc $(CARD_INCLUDE) -o $@ test/card_tests.cpp src/CardFs.cpp \
		$(VFS_LIB) $(FSTOOLS_LIB) $(COMMON_LIB)

card-test: $(CARD_TEST_BIN)
	./$(CARD_TEST_BIN)
.PHONY: card-test

# --- artwork ----------------------------------------------------------------
assets/silkscreen.pgm: tools/silkscreen2pgm.py
	@mkdir -p assets
	python3 tools/silkscreen2pgm.py $(CP_EMU)/native/Silkscreen.cpp $@

# --- Kobo package / release -------------------------------------------------
VERSION := $(shell sed -n 's/^\#define PALMKOBO_VERSION "\(.*\)"/\1/p' src/Version.h)

# Lay out the files that go on the Kobo's drive under $(1) (i.e. $(1)/.adds/...).
define stage_kobo_files
	mkdir -p $(1)/.adds/palmkobo/install $(1)/.adds/palmkobo/to-card $(1)/.adds/nm
	cp $(BUILD)/palmkobo $(1)/.adds/palmkobo/
	cp kobo/palmkobo.sh kobo/palmkobo.ini assets/silkscreen.pgm $(1)/.adds/palmkobo/
	cp kobo/README-device.txt $(1)/.adds/palmkobo/README.txt
	cp kobo/nickelmenu-palmkobo $(1)/.adds/nm/palmkobo
	chmod 755 $(1)/.adds/palmkobo/palmkobo $(1)/.adds/palmkobo/palmkobo.sh
endef

# Quick install zip for your own Kobo: unzip into the root of the Kobo's drive.
package: all assets/silkscreen.pgm
	rm -rf build/package
	$(call stage_kobo_files,build/package)
	cd build/package && rm -f ../palmkobo-kobo.zip && zip -qr ../palmkobo-kobo.zip .adds
	@echo "=> build/palmkobo-kobo.zip (unzip into the root of the Kobo's USB drive)"

# Shareable release: user guide, licence, exact upstream versions, and the files
# for the Kobo in a KOBO/ folder. Pair it with the source zip from "make dist".
REL_NAME := palmkobo-$(VERSION)
REL_DIR := build/release/$(REL_NAME)
release: check-release all assets/silkscreen.pgm
	rm -rf build/release
	$(call stage_kobo_files,$(REL_DIR)/KOBO)
	cp docs/USER-GUIDE.txt $(REL_DIR)/README.txt
	cp LICENSE $(REL_DIR)/LICENSE.txt
	@{ \
	  echo "palmkobo $(VERSION) - build information"; \
	  echo; \
	  echo "Built: $$(date -u '+%Y-%m-%d %H:%M UTC')"; \
	  echo "Compiler: $$($(CXX) --version | head -1)"; \
	  echo; \
	  echo "This binary contains code from the following projects, all licensed under"; \
	  echo "the GNU GPL version 3 (see LICENSE.txt). The complete corresponding source"; \
	  echo "is palmkobo-$(VERSION)-src.zip together with these exact upstream versions:"; \
	  echo; \
	  echo "  palmkobo      $(VERSION)$$(git -C . rev-parse --short HEAD 2>/dev/null | sed 's/^/ (git /;s/$$/)/')"; \
	  echo "  CloudpilotEmu https://github.com/cloudpilot-emu/cloudpilot"; \
	  echo "                commit $$(git -C $(CLOUDPILOT) rev-parse HEAD 2>/dev/null || echo unknown)$$(git -C $(CLOUDPILOT) diff --quiet 2>/dev/null || echo ' (with local changes)')"; \
	  echo "  FBInk         https://github.com/NiLuJe/FBInk"; \
	  echo "                commit $$(git -C $(FBINK) rev-parse HEAD 2>/dev/null || echo unknown)$$(git -C $(FBINK) diff --quiet 2>/dev/null || echo ' (with local changes)')"; \
	  echo; \
	  echo "No Palm OS ROMs are included. Users must supply ROMs dumped from their own devices."; \
	} > $(REL_DIR)/SOURCES.txt
	cd build/release && rm -f ../$(REL_NAME)-kobo.zip && zip -qr ../$(REL_NAME)-kobo.zip $(REL_NAME)
	@echo "=> build/$(REL_NAME)-kobo.zip"
	@echo "   Share it together with the source zip: make dist"

check-release:
ifneq ($(TARGET),kobo)
ifndef ALLOW_HOST_RELEASE
	@echo "release needs TARGET=kobo (a desktop binary won't run on a Kobo)"; exit 1
endif
endif

# Source zip (required by the GPL when you share binaries).
dist:
	rm -rf build/dist && mkdir -p build/dist/$(REL_NAME)-src
	cp -r Makefile README.md LICENSE src mk kobo tools test docs assets build/dist/$(REL_NAME)-src/
	cd build/dist && rm -f ../$(REL_NAME)-src.zip && zip -qr ../$(REL_NAME)-src.zip $(REL_NAME)-src
	@echo "=> build/$(REL_NAME)-src.zip"

clean:
	rm -rf build
	-rm -rf $(CP_EMU)/.build-palmkobo-* $(CP_EMU)/.deps-palmkobo-* \
		$(CP_COMMON)/.build-palmkobo-* $(CP_COMMON)/.deps-palmkobo-* \
		$(CP_VFS)/.build-palmkobo-* $(CP_VFS)/.deps-palmkobo-* \
		$(CP_FSTOOLS)/.build-palmkobo-* $(CP_FSTOOLS)/.deps-palmkobo-*

FORCE:
.PHONY: all test clean package release check-release dist check-cloudpilot FORCE

-include $(OBJ:.o=.d)

# --- screen mock-up (host only) ----------------------------------------------
preview: assets/silkscreen.pgm
	@mkdir -p build/test
	g++ -std=c++17 -O1 -Isrc -o build/test/preview test/preview.cpp src/ButtonBar.cpp
	./build/test/preview assets/silkscreen.pgm build/test/preview.pgm
.PHONY: preview
