CXX=g++
CXXFLAGS=-std=c++17 -Wall -Wextra -pedantic

SRCDIR=src
BINDIR=x64
TESTDIR=tests
TESTINCLUDES=-I $(TESTDIR)/external/Catch2/ -I $(SRCDIR)

$(BINDIR)/main: $(wildcard $(SRCDIR)/*.cpp)
	mkdir -p $(BINDIR)
	$(CXX) $(CXXFLAGS) -o $@ $^

$(BINDIR)/test: $(TESTDIR)/FlashCppTest/FlashCppTest/FlashCppTest/FlashCppTest.cpp
	mkdir -p $(BINDIR)
	$(CXX) $(CXXFLAGS) $(TESTINCLUDES) -o $@ $^

.PHONY: clean

clean:
	rm -rf $(BINDIR)

test: $(BINDIR)/test