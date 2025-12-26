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

# Compiler flags - add -Wall -Wextra -pedantic for better warnings
CXXFLAGS := -std=c++20

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
BENCHMARK_DIR := $(BINDIR)/Benchmark

# Source files needed for the test (excluding main.cpp, benchmark.cpp, LibClangIRGenerator.cpp)
TEST_SOURCES := $(SRCDIR)/AstNodeTypes.cpp $(SRCDIR)/ChunkedAnyVector.cpp $(SRCDIR)/Parser.cpp $(SRCDIR)/CodeViewDebug.cpp $(SRCDIR)/LazyMemberResolver.cpp $(SRCDIR)/InstantiationQueue.cpp $(SRCDIR)/ExpressionSubstitutor.cpp

# Main sources (excluding LLVM-dependent files)
MAIN_SOURCES := $(SRCDIR)/AstNodeTypes.cpp $(SRCDIR)/ChunkedAnyVector.cpp $(SRCDIR)/Parser.cpp $(SRCDIR)/CodeViewDebug.cpp $(SRCDIR)/LazyMemberResolver.cpp $(SRCDIR)/InstantiationQueue.cpp $(SRCDIR)/ExpressionSubstitutor.cpp $(SRCDIR)/main.cpp

# Target executables with proper extensions (matching MSVC structure)
MAIN_TARGET := $(DEBUG_DIR)/FlashCpp$(EXE_EXT)
RELEASE_TARGET := $(RELEASE_DIR)/FlashCpp$(EXE_EXT)
TEST_TARGET := $(TEST_DIR)/test$(EXE_EXT)
BENCHMARK_TARGET := $(BENCHMARK_DIR)/benchmark$(EXE_EXT)

# Default target
.DEFAULT_GOAL := main

# Build main executable (Debug configuration)
$(MAIN_TARGET): $(MAIN_SOURCES)
	@echo "Building main executable (Debug) for $(PLATFORM) with $(CXX)..."
	@$(MKDIR) $(DEBUG_DIR) 2>nul || $(MKDIR) $(DEBUG_DIR) || true
	$(CXX) $(CXXFLAGS) $(INCLUDES) -g -o $@ $^
	@echo "Built: $@"

# Build release executable
$(RELEASE_TARGET): $(MAIN_SOURCES)
	@echo "Building main executable (Release) for $(PLATFORM) with $(CXX)..."
	@$(MKDIR) $(RELEASE_DIR) 2>nul || $(MKDIR) $(RELEASE_DIR) || true
	$(CXX) $(CXXFLAGS) $(INCLUDES) -O2 -o $@ $^
	@echo "Built: $@"

# Build test executable
$(TEST_TARGET): $(TESTDIR)/FlashCppTest/FlashCppTest/FlashCppTest/FlashCppTest.cpp $(TEST_SOURCES)
	@echo "Building test executable for $(PLATFORM) with $(CXX)..."
	@$(MKDIR) $(TEST_DIR) 2>nul || $(MKDIR) $(TEST_DIR) || true
	$(CXX) $(CXXFLAGS) $(TESTINCLUDES) -O1 -g -o $@ $^
	@echo "Built: $@"

# Build benchmark executable (requires LLVM)
$(BENCHMARK_TARGET): $(SRCDIR)/benchmark.cpp
	@echo "Building benchmark executable for $(PLATFORM) with $(CXX)..."
	@$(MKDIR) $(BENCHMARK_DIR) 2>nul || $(MKDIR) $(BENCHMARK_DIR) || true
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LLVM_LIBS)
	@echo "Built: $@"

# Phony targets
.PHONY: all clean test main release benchmark help test-all

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

# Benchmark target
benchmark: $(BENCHMARK_TARGET)

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
	@echo "  x64/Benchmark/benchmark$(EXE_EXT) - Benchmark build"
	@echo ""
	@echo "Available targets:"
	@echo "  make              - Build main executable in Debug mode (default)"
	@echo "  make main         - Build main executable in Debug mode"
	@echo "  make release      - Build main executable in Release mode"
	@echo "  make test         - Build test executable"
	@echo "  make test-all     - Build compiler and run all .cpp tests (pass and _fail)"
	@echo "  make benchmark    - Build benchmark executable"
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
