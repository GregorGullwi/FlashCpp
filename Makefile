CXX=g++
CXXFLAGS=-std=c++17 -Wall -Wextra -pedantic

SRCDIR=src
BINDIR=x64
TESTDIR=tests
TESTINCLUDES=-I $(TESTDIR)/external/doctest/ -I $(SRCDIR) -I external

$(BINDIR)/main: $(wildcard $(SRCDIR)/*.cpp)
	mkdir -p $(BINDIR)
	$(CXX) $(CXXFLAGS) -o $@ $^

$(BINDIR)/test: $(TESTDIR)/FlashCppTest/FlashCppTest/FlashCppTest/FlashCppTest.cpp
	mkdir -p $(BINDIR)
	$(CXX) $(CXXFLAGS) $(TESTINCLUDES) -O1 -g -o $@ $^

$(BINDIR)/main-debug: $(TESTDIR)/FlashCppTest/FlashCppTest/FlashCppTest/FlashCppTest.cpp
	mkdir -p $(BINDIR)
	$(CXX) $(CXXFLAGS) $(TESTINCLUDES) -O0 -g -o $@ $^

$(BINDIR)/benchmark: $(SRCDIR)/benchmark.cpp
	mkdir -p $(BINDIR)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LLVM_LIBS)

.PHONY: clean

clean:
	rm -rf $(BINDIR)

test: $(BINDIR)/test

main-debug: $(BINDIR)/main-debug
	$(BINDIR)/main-debug
