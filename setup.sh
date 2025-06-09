#!/bin/bash

# Update package lists
sudo apt-get update

# Install essential build tools and Clang
sudo apt-get install -y build-essential make

# Install Clang and related tools
sudo apt-get install -y clang clang++ libc++-dev libc++abi-dev

# Install a newer version of Clang if available
sudo apt-get install -y clang-15 clang++-15 || sudo apt-get install -y clang-14 clang++-14 || echo "Using default clang version"

# Try to set up the newest available Clang as default
if command -v clang-15 &> /dev/null; then
    sudo update-alternatives --install /usr/bin/clang clang /usr/bin/clang-15 100
    sudo update-alternatives --install /usr/bin/clang++ clang++ /usr/bin/clang++-15 100
elif command -v clang-14 &> /dev/null; then
    sudo update-alternatives --install /usr/bin/clang clang /usr/bin/clang-14 100
    sudo update-alternatives --install /usr/bin/clang++ clang++ /usr/bin/clang++-14 100
fi

# Verify the installation
clang++ --version

# Create necessary directories
mkdir -p x64

# Create the Makefile configured for Clang with C++20
cat > Makefile << 'EOF'
CXX=clang++
CXXFLAGS=-std=c++20 -Wall -Wextra -pedantic -stdlib=libc++

SRCDIR=src
BINDIR=x64
TESTDIR=tests
TESTINCLUDES=-I $(TESTDIR)/external/doctest/ -I $(SRCDIR) -I external

# Source files needed for the test (excluding main.cpp, benchmark.cpp, LibClangIRGenerator.cpp)
TEST_SOURCES=$(SRCDIR)/AstNodeTypes.cpp $(SRCDIR)/ChunkedAnyVector.cpp $(SRCDIR)/Parser.cpp

$(BINDIR)/main: $(wildcard $(SRCDIR)/*.cpp)
	mkdir -p $(BINDIR)
	$(CXX) $(CXXFLAGS) -o $@ $^

$(BINDIR)/test: $(TESTDIR)/FlashCppTest/FlashCppTest/FlashCppTest/FlashCppTest.cpp $(TEST_SOURCES)
	mkdir -p $(BINDIR)
	$(CXX) $(CXXFLAGS) $(TESTINCLUDES) -O1 -g -o $@ $^

$(BINDIR)/main-debug: $(TESTDIR)/FlashCppTest/FlashCppTest/FlashCppTest/FlashCppTest.cpp $(TEST_SOURCES)
	mkdir -p $(BINDIR)
	$(CXX) $(CXXFLAGS) $(TESTINCLUDES) -O0 -g -o $@ $^

$(BINDIR)/benchmark: $(SRCDIR)/benchmark.cpp
	mkdir -p $(BINDIR)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LLVM_LIBS)

.PHONY: clean test main-debug

clean:
	rm -rf $(BINDIR)

test: $(BINDIR)/test

main-debug: $(BINDIR)/main-debug
	$(BINDIR)/main-debug
EOF

# Add current directory to PATH in case it's needed
echo 'export PATH="$PWD:$PATH"' >> ~/.profile

# Source the profile to make changes available immediately
source ~/.profile

echo "Setup complete. C++ build environment with Clang, C++20, and libc++ ready."