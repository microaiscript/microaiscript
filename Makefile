# AiScript C Makefile

CC = cc
CFLAGS = -Wall -Wextra -std=gnu11 -O2 -Iinclude -Os -ffunction-sections -fdata-sections -fomit-frame-pointer
# Uncomment to disable Python-like error messages:
# CFLAGS += -DNO_PYTHONIC_ERRORS

# exclude -rdynamic and -ldl for windows aswell as staically link libreadline
ifeq ($(OS),Windows_NT)
LDFLAGS = -lm -lpthread -Wl,--gc-sections -Wl,-Map=$(TARGET).map -Wl,-Bstatic -lreadline -lncurses  -Wl,-Bdynamic
else
LDFLAGS = -lm -ldl -lpthread -rdynamic -Wl,--gc-sections -Wl,-Map=$(TARGET).map
endif

# Check for readline library
ifeq ($(shell pkg-config --exists readline && echo yes),yes)
    CFLAGS += -DHAVE_READLINE $(shell pkg-config --cflags readline)
    LDFLAGS += $(shell pkg-config --libs readline)
else ifeq ($(shell echo '\#include <readline/readline.h>' | $(CC) -E - > /dev/null 2>&1 && echo yes),yes)
    CFLAGS += -DHAVE_READLINE
    LDFLAGS += -lreadline
endif

# Directories
SRC_DIR = src
INC_DIR = include
BUILD_DIR = build
BIN_DIR = bin
TEST_DIR = test
TEST_BUILD_DIR = $(BUILD_DIR)/test

# Source files
SRCS = $(wildcard $(SRC_DIR)/*.c)
OBJS = $(SRCS:$(SRC_DIR)/%.c=$(BUILD_DIR)/%.o)
ifeq ($(OS),Windows_NT)
RES_OBJS = $(patsubst $(SRC_DIR)/icon/%.rc,$(BUILD_DIR)/%.res,$(filter-out $(SRC_DIR)/icon/%-dll.rc,$(wildcard $(SRC_DIR)/icon/*.rc)))
LIB_RES_OBJS = $(patsubst $(SRC_DIR)/icon/%-dll.rc,$(BUILD_DIR)/%-dll.res,$(wildcard $(SRC_DIR)/icon/*-dll.rc))
endif
DEPS = $(wildcard $(INC_DIR)/*.h)

# Test files
TEST_SRCS = $(wildcard $(TEST_DIR)/*.c)
TEST_OBJS = $(TEST_SRCS:$(TEST_DIR)/%.c=$(TEST_BUILD_DIR)/%.o)

# Target executables
TARGET = $(BIN_DIR)/aiscript
TEST_TARGET = $(BIN_DIR)/test_aiscript
VM_RUNNER = $(BIN_DIR)/ais-vm
CONFORMANCE_TARGET = $(BIN_DIR)/test_conformance

# Library targets
ifeq ($(OS),Windows_NT)
LIB_TARGET_SO = $(BIN_DIR)/libaiscript.dll
else ifeq ($(OS),Darwin)
LIB_TARGET_SO = $(BIN_DIR)/libaiscript.dylib
else
LIB_TARGET_SO = $(BIN_DIR)/libaiscript.so
endif
LIB_TARGET_A  = $(BIN_DIR)/libaiscript.a
LIB_SRCS = $(filter-out $(SRC_DIR)/main.c, $(SRCS))
LIB_OBJS = $(LIB_SRCS:$(SRC_DIR)/%.c=$(BUILD_DIR)/%-pic.o)

# Install paths (can be overridden: make install PREFIX=/usr)
PREFIX     ?= /usr/local
LIBDIR     ?= $(PREFIX)/lib
INCLUDEDIR ?= $(PREFIX)/include/aiscript
PKGCFGDIR  ?= $(LIBDIR)/pkgconfig

# Default target
all: $(TARGET)

# Create directories
$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

$(TEST_BUILD_DIR):
	mkdir -p $(TEST_BUILD_DIR)

# Build object files
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c $(DEPS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

ifeq ($(OS),Windows_NT)
# build resource files (program)
$(BUILD_DIR)/%.res: $(SRC_DIR)/icon/%.rc | $(BUILD_DIR)
	windres -i $< -o $@ -O coff

# build resource files (library)
$(BUILD_DIR)/%-dll.res: $(SRC_DIR)/icon/%-dll.rc | $(BUILD_DIR)
	windres -i $< -o $@ -O coff
endif

# Build PIC object files (for shared library)
# The library must not depend on GNU Readline (only the CLI uses it for line editing)
$(BUILD_DIR)/%-pic.o: $(SRC_DIR)/%.c $(DEPS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -UHAVE_READLINE -fPIC -c $< -o $@

# Build test object files
$(TEST_BUILD_DIR)/%.o: $(TEST_DIR)/%.c $(DEPS) | $(TEST_BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# Link executable
ifeq ($(OS),Windows_NT)
$(TARGET): $(OBJS) $(RES_OBJS) | $(BIN_DIR)
	$(CC) $(OBJS) $(RES_OBJS) -o $@ $(LDFLAGS)
else
$(TARGET): $(OBJS) | $(BIN_DIR)
	$(CC) $(OBJS) -o $@ $(LDFLAGS)
endif

# Link test executable (exclude main.o from OBJS)
$(TEST_TARGET): $(filter-out $(BUILD_DIR)/main.o,$(OBJS)) $(TEST_OBJS) | $(BIN_DIR)
	$(CC) $(filter-out $(BUILD_DIR)/main.o,$(OBJS)) $(TEST_OBJS) -o $@ $(LDFLAGS)

# Build tests
test_build: $(TEST_TARGET)

# Link VM runner executable
$(VM_RUNNER): src/tools/ais-vm.c $(filter-out $(BUILD_DIR)/main.o,$(OBJS)) | $(BIN_DIR)
	$(CC) $(CFLAGS) src/tools/ais-vm.c $(filter-out $(BUILD_DIR)/main.o,$(OBJS)) -o $@ $(LDFLAGS)

# Build VM runner
vm: $(VM_RUNNER)
	@echo "Built ais-vm successfully"

# Basic bytecode cases the WIP VM is expected to handle
test_basic_vm: vm
	@chmod +x test/test_basic_vm.sh
	@test/test_basic_vm.sh

# Native modules used by the script regression tests (built into test/regression/)
REGRESSION_MODULES = test/regression/hello.so test/regression/math_ext.so

test/regression/%.so: examples/native/%.c $(DEPS)
	$(CC) $(CFLAGS) -fPIC -shared -o $@ $<

# Script-level regression tests (test/regression/*.ais must exit 0)
test_scripts: $(TARGET) $(REGRESSION_MODULES)
	@chmod +x test/run_regression.sh
	@test/run_regression.sh $(abspath $(TARGET)) test/regression

# Dual-path interpreter + VM conformance suite (aiscript-rs fixtures)
$(CONFORMANCE_TARGET): test/conformance/runner.c aic-cross/compiler.c aic-cross/optimizer.c lib/cJSON.c $(filter-out $(BUILD_DIR)/main.o,$(OBJS)) | $(BIN_DIR)
	$(CC) $(CFLAGS) -Ilib -DCONFORMANCE_DIR=\"test/conformance\" \
		test/conformance/runner.c aic-cross/compiler.c aic-cross/optimizer.c lib/cJSON.c \
		$(filter-out $(BUILD_DIR)/main.o,$(OBJS)) -o $@ $(LDFLAGS)

test_conformance: $(CONFORMANCE_TARGET)
	@echo "Running AiScript conformance suite..."
	@$(CONFORMANCE_TARGET)

# Build shared library
ifeq ($(OS),Windows_NT)
$(LIB_TARGET_SO): $(LIB_OBJS) $(LIB_RES_OBJS) | $(BIN_DIR)
	$(CC) -shared -fPIC $(LIB_OBJS) $(LIB_RES_OBJS) -o $@ $(LDFLAGS)
	@echo "Built $(LIB_TARGET_SO)"
else
$(LIB_TARGET_SO): $(LIB_OBJS) | $(BIN_DIR)
	$(CC) -shared -fPIC $(LIB_OBJS) -o $@ $(LDFLAGS)
	@echo "Built $(LIB_TARGET_SO)"
endif

# Build static library
$(LIB_TARGET_A): $(LIB_OBJS) | $(BIN_DIR)
	$(AR) rcs $@ $(LIB_OBJS)
	@echo "Built $(LIB_TARGET_A)"

# Build both libraries
lib: $(LIB_TARGET_SO) $(LIB_TARGET_A)
	@echo "Libraries ready in $(BIN_DIR)/"

# Run tests
test: $(TEST_TARGET)
	@echo "Running AiScript C test suite..."
	@$(TEST_TARGET)

# Clean build artifacts
clean:
	rm -rf $(BUILD_DIR) $(BIN_DIR)

# Install interpreter
install: $(TARGET)
	install -m 755 $(TARGET) $(PREFIX)/bin/aiscript

# Install libraries + headers + pkg-config file
install-lib: lib
	install -d $(LIBDIR) $(INCLUDEDIR) $(PKGCFGDIR)
	install -m 755 $(LIB_TARGET_SO) $(LIBDIR)/libaiscript.so
	install -m 644 $(LIB_TARGET_A)  $(LIBDIR)/libaiscript.a
	install -m 644 $(INC_DIR)/aiscript.h  $(INCLUDEDIR)/aiscript.h
	install -m 644 $(INC_DIR)/types.h     $(INCLUDEDIR)/types.h
	install -m 644 $(INC_DIR)/interpreter.h $(INCLUDEDIR)/interpreter.h
	install -m 644 $(INC_DIR)/scope.h     $(INCLUDEDIR)/scope.h
	install -m 644 $(INC_DIR)/node.h      $(INCLUDEDIR)/node.h
	@printf 'prefix=%s\nlibdir=%s\nincludedir=%s\n\nName: libaiscript\nDescription: AiScript C interpreter library\nVersion: 1.3.0\nLibs: -L%s -laiscript -lm -ldl -lpthread\nCflags: -I%s\n' \
		$(PREFIX) $(LIBDIR) $(INCLUDEDIR) $(LIBDIR) $(INCLUDEDIR) \
		> $(PKGCFGDIR)/aiscript.pc
	@echo "Installed libaiscript to $(LIBDIR)"
	@echo "pkg-config file written to $(PKGCFGDIR)/aiscript.pc"

# Uninstall interpreter
uninstall:
	rm -f $(PREFIX)/bin/aiscript

# Uninstall libraries
uninstall-lib:
	rm -f $(LIBDIR)/libaiscript.so $(LIBDIR)/libaiscript.a
	rm -rf $(INCLUDEDIR)
	rm -f $(PKGCFGDIR)/aiscript.pc

# Run examples
examples: $(TARGET)
	@echo "Running Hello World example..."
	@$(TARGET) examples/hello.ais
	@echo ""
	@echo "Running FizzBuzz example..."
	@$(TARGET) examples/fizzbuzz.ais
	@echo ""
	@echo "Running arithmetic example..."
	@$(TARGET) examples/arithmetic.ais

# Help
help:
	@echo "AiScript C Build System"
	@echo ""
	@echo "Targets:"
	@echo "  all            - Build the AiScript interpreter (default)"
	@echo "  lib            - Build libaiscript.so and libaiscript.a"
	@echo "  vm             - Build the bytecode VM (ais-vm)"
	@echo "  clean          - Remove build artifacts"
	@echo "  install        - Install aiscript to PREFIX/bin (default /usr/local)"
	@echo "  install-lib    - Install libraries, headers, and pkg-config file"
	@echo "  uninstall      - Remove interpreter from PREFIX/bin"
	@echo "  uninstall-lib  - Remove libraries, headers, and pkg-config file"
	@echo "  test             - Run interpreter unit tests"
	@echo "  test_conformance - Run aiscript-rs dual-path interpreter/VM suite"
	@echo "  test_basic_vm    - Run basic bytecode VM smoke tests"
	@echo "  test_scripts     - Run script regression tests in test/regression"
	@echo "  examples       - Run example programs"
	@echo "  help           - Show this help message"
	@echo ""
	@echo "Variables:"
	@echo "  PREFIX=$(PREFIX)  LIBDIR=$(LIBDIR)"
	@echo "  INCLUDEDIR=$(INCLUDEDIR)"

.PHONY: all clean install uninstall install-lib uninstall-lib lib test test_build test_basic_vm test_conformance test_scripts vm examples help

