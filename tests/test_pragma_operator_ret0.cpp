// Test: _Pragma() operator (C++20 §15.9 [cpp.pragma.op])
// Regression test for _Pragma being handled in the preprocessor
_Pragma("warning(push)")
_Pragma("warning(disable : 4996)")

static_assert(true, "after _Pragma");

_Pragma("warning(pop)")

int main() { return 0; }
