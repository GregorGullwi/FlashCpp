# Detect OS and set platform-specific variables
# Check for native Windows (cmd.exe or PowerShell)
ifeq ($(OS),Windows_NT)
    # Native Windows (not WSL)
    PLATFORM := Windows
    EXE_EXT := .exe
    MKDIR := mkdir
    RM := del /Q /F
    RMDIR := rmdir /S /Q
    PATH_SEP := \\
    SHELL := cmd.exe
else
    # Unix-like systems (Linux, macOS, WSL)
    UNAME_S := $(shell uname -s)

    # Check if we're in WSL
    ifeq ($(shell uname -r | grep -i microsoft),)
        # Native Linux or macOS
        PLATFORM := $(UNAME_S)
        EXE_EXT :=
    else
        # WSL - treat like Linux but note it in platform
        PLATFORM := WSL
        EXE_EXT :=
    endif

    MKDIR := mkdir -p
    RM := rm -f
    RMDIR := rm -rf
    PATH_SEP := /
endif

# Compiler selection - default to clang++, but allow override
# Usage: make CXX=g++ to use gcc instead
CXX ?= clang++

# Compiler flags - enable strict warnings for clean code
CXXFLAGS := -std=c++20 -Wall -Wextra -Wshadow -Werror

# Directories
SRCDIR := src
BINDIR := x64
TESTDIR := tests
INCLUDES := -I $(SRCDIR)
TESTINCLUDES := -I $(TESTDIR)/external/doctest/ -I $(SRCDIR) -I external

# Build configuration subdirectories (matching MSVC structure)
DEBUG_DIR := $(BINDIR)/Debug
RELEASE_DIR := $(BINDIR)/Release
TEST_DIR := $(BINDIR)/Test

# All source files in the src directory (for dependency tracking with unity builds)
# Using wildcard ensures any header or source change triggers a rebuild
UNITY_SOURCES := $(wildcard $(SRCDIR)/*.h) $(wildcard $(SRCDIR)/*.cpp)

# Source files needed for the test (unity build - only FlashCppTest.cpp is compiled)
TEST_SOURCES :=

# Main sources (unity build - only main.cpp is compiled)
MAIN_SOURCES := $(SRCDIR)/main.cpp

# Target executables with proper extensions (matching MSVC structure)
MAIN_TARGET := $(DEBUG_DIR)/FlashCpp$(EXE_EXT)
RELEASE_TARGET := $(RELEASE_DIR)/FlashCpp$(EXE_EXT)
TEST_TARGET := $(TEST_DIR)/test$(EXE_EXT)

# Default target
.DEFAULT_GOAL := main

# Marker file to track ASAN builds
ASAN_MARKER := $(DEBUG_DIR)/.asan_build

# Build main executable (Debug configuration)
# The marker file dependency forces a rebuild when switching from ASAN to non-ASAN
$(MAIN_TARGET): $(MAIN_SOURCES) $(UNITY_SOURCES) | check-asan-marker
	@echo "Building main executable (Debug) for $(PLATFORM) with $(CXX)..."
	@$(MKDIR) $(DEBUG_DIR) 2>nul || $(MKDIR) $(DEBUG_DIR) || true
	$(CXX) $(CXXFLAGS) $(INCLUDES) -g -o $@ $(MAIN_SOURCES)
	@rm -f $(ASAN_MARKER)
	@echo "Built: $@"

# Check if ASAN marker exists and force rebuild by removing target if it does
.PHONY: check-asan-marker
check-asan-marker:
	@if [ -f $(ASAN_MARKER) ]; then \
		echo "ASAN marker detected, removing target to force non-ASAN rebuild..."; \
		rm -f $(MAIN_TARGET); \
	fi

# ASAN target - builds to same binary as main target but with AddressSanitizer
# Creates marker file to track that this build includes ASAN
.PHONY: asan
asan: check-non-asan-marker
	@echo "Building main executable (Debug+ASAN) for $(PLATFORM) with $(CXX)..."
	@$(MKDIR) $(DEBUG_DIR) 2>nul || $(MKDIR) $(DEBUG_DIR) || true
	$(CXX) $(CXXFLAGS) -fsanitize=address -fno-omit-frame-pointer $(INCLUDES) -g -o $(MAIN_TARGET) $(MAIN_SOURCES)
	@touch $(ASAN_MARKER)
	@echo "Built: $(MAIN_TARGET)"

# Check if ASAN marker is missing and force rebuild by removing target if it is
.PHONY: check-non-asan-marker
check-non-asan-marker:
	@if [ ! -f $(ASAN_MARKER) ]; then \
		echo "ASAN marker not found, removing target to force ASAN rebuild..."; \
		rm -f $(MAIN_TARGET); \
	fi

# Build release executable
$(RELEASE_TARGET): $(MAIN_SOURCES) $(UNITY_SOURCES)
	@echo "Building main executable (Release) for $(PLATFORM) with $(CXX)..."
	@$(MKDIR) $(RELEASE_DIR) 2>nul || $(MKDIR) $(RELEASE_DIR) || true
	$(CXX) $(CXXFLAGS) -Wno-unused-parameter $(INCLUDES) -DNDEBUG -DFLASHCPP_LOG_LEVEL=1 -O3 -o $@ $(MAIN_SOURCES)
	@echo "Built: $@"

# Build test executable
$(TEST_TARGET): $(TESTDIR)/FlashCppTest/FlashCppTest/FlashCppTest/FlashCppTest.cpp $(UNITY_SOURCES)
	@echo "Building test executable for $(PLATFORM) with $(CXX)..."
	@$(MKDIR) $(TEST_DIR) 2>nul || $(MKDIR) $(TEST_DIR) || true
	$(CXX) $(CXXFLAGS) $(TESTINCLUDES) -O1 -g -o $@ $(TESTDIR)/FlashCppTest/FlashCppTest/FlashCppTest/FlashCppTest.cpp
	@echo "Built: $@"

# Phony targets
.PHONY: all clean test main release help test-all asan

# Build all targets
all: main release test

# Main target (Debug configuration)
main: $(MAIN_TARGET)

# Release target
release: $(RELEASE_TARGET)

# Test target - build and optionally run
test: $(TEST_TARGET)

# Test-all target - build compiler and run all .cpp tests in tests/
test-all: $(MAIN_TARGET)
	@echo "Running comprehensive test suite..."
	@bash $(TESTDIR)/run_all_tests.sh

# Clean build artifacts
clean:
	@echo "Cleaning build artifacts..."
ifeq ($(PLATFORM),Windows)
	@if exist $(BINDIR) $(RMDIR) $(BINDIR) 2>nul || echo "Already clean"
else
	@$(RMDIR) $(BINDIR) 2>/dev/null || echo "Already clean"
endif
	@echo "Clean complete"

# Help target
help:
	@echo "FlashCpp Makefile - Cross-platform build system"
	@echo ""
	@echo "Platform detected: $(PLATFORM)"
	@echo "Compiler: $(CXX)"
	@echo ""
	@echo "Output structure (matching MSVC):"
	@echo "  x64/Debug/FlashCpp$(EXE_EXT)      - Debug build"
	@echo "  x64/Release/FlashCpp$(EXE_EXT)    - Release build"
	@echo "  x64/Test/test$(EXE_EXT)           - Test build"
	@echo ""
	@echo "Available targets:"
	@echo "  make              - Build main executable in Debug mode (default)"
	@echo "  make main         - Build main executable in Debug mode"
	@echo "  make asan         - Build main executable in Debug mode with AddressSanitizer"
	@echo "  make release      - Build main executable in Release mode"
	@echo "  make test         - Build test executable"
	@echo "  make test-all     - Build compiler and run all .cpp tests (pass and _fail)"
	@echo "  make all          - Build main, release, and test executables"
	@echo "  make clean        - Remove all build artifacts"
	@echo "  make help         - Show this help message"
	@echo ""
	@echo "Compiler selection:"
	@echo "  make CXX=g++      - Build with g++ instead of clang++"
	@echo "  make CXX=clang++  - Build with clang++ (default)"
	@echo ""
	@echo "Examples:"
	@echo "  make clean && make                    - Clean debug build with clang++"
	@echo "  make clean && make release            - Clean release build with clang++"
	@echo "  make clean && make CXX=g++            - Clean build with g++"
	@echo "  make test && $(TEST_DIR)/test$(EXE_EXT)  - Build and run tests"
	@echo "  make test-all                         - Run all .cpp tests including _fail tests"
	@echo "  $(MAIN_TARGET) file.cpp               - Compile a C++ file"
