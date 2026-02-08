// Bug: Variadic template recursion produces incorrect results at runtime
// Status: RUNTIME FAILURE - Compiles and links but returns wrong value
// Date: 2026-02-07
//
// Recursive variadic templates using parameter pack expansion should work
// correctly when the base case and recursive case are separate overloads.

template<typename T>
T var_sum(T val) {
    return val;
}

template<typename T, typename... Rest>
T var_sum(T first, Rest... rest) {
    return first + var_sum(rest...);
}

int main() {
    int result = var_sum(1, 2, 3, 4);
    return result == 10 ? 0 : 1;
}

// Expected behavior (with clang++/g++):
// Compiles and runs successfully, returns 0 (result = 1+2+3+4 = 10)
//
// Actual behavior (with FlashCpp):
// Compiles and links without errors, but returns 1 at runtime.
// The recursive template expansion produces an incorrect sum.
//
// Note: Non-recursive variadic patterns like fold expressions work correctly:
//   template<typename... Args>
//   auto fold_add(Args... args) { return (args + ...); }
//
// Workaround: Use fold expressions instead of recursive expansion.
//
// Fix: Investigate parameter pack expansion in recursive template calls.
// The issue may be in how intermediate template instantiations handle
// the pack expansion or how the recursive call's return value is used.
