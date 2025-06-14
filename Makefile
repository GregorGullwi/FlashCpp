CXX=clang++
CXXFLAGS=-std=c++20 -Wall -Wextra -pedantic

SRCDIR=src
BINDIR=x64
TESTDIR=tests
TESTINCLUDES=-I $(TESTDIR)/external/doctest/ -I $(SRCDIR) -I external

# Source files needed for the test (excluding main.cpp, benchmark.cpp, LibClangIRGenerator.cpp)
TEST_SOURCES=$(SRCDIR)/AstNodeTypes.cpp $(SRCDIR)/ChunkedAnyVector.cpp $(SRCDIR)/Parser.cpp

# Main sources (excluding LLVM-dependent files)
MAIN_SOURCES=$(SRCDIR)/AstNodeTypes.cpp $(SRCDIR)/ChunkedAnyVector.cpp $(SRCDIR)/Parser.cpp $(SRCDIR)/CodeViewDebug.cpp $(SRCDIR)/main.cpp

$(BINDIR)/main: $(MAIN_SOURCES)
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
