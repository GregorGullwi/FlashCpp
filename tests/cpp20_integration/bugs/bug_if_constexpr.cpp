// Bug: if constexpr causes compilation issues in templates
// Status: LINK ERROR - Generates code but doesn't link
// Date: 2025-12-02
//
// Minimal reproduction case for FlashCpp issue when using
// if constexpr in template functions.

template<typename T>
T simple_value(T val) {
    return val;
}

template<typename T, typename... Rest>
T variadic_sum(T first, Rest... rest) {
    if constexpr (sizeof...(rest) == 0) {
        return first;
    } else {
        return first + variadic_sum(rest...);
    }
}

int main() {
    int result = variadic_sum(1, 2, 3, 4);
    return result == 10 ? 0 : 1;
}

// Expected behavior (with clang++):
// Compiles and runs successfully, returns 0
//
// Actual behavior (with FlashCpp):
// Parses successfully but generates code that doesn't link
// Unresolved symbols during linking phase
//
// Workaround:
// Use regular template recursion without if constexpr
