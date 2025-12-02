// Bug: sizeof... operator causes crashes in variadic templates
// Status: CRASH - FlashCpp segmentation fault during code generation
// Date: 2025-12-02
//
// Minimal reproduction case for FlashCpp crash when using sizeof...
// operator in variadic template functions.

template<typename... Args>
int count_args(Args... args) {
    return sizeof...(args);  // This line causes the crash
}

int main() {
    int result = count_args(1, 2, 3, 4, 5);
    return result == 5 ? 0 : 1;
}

// Expected behavior (with clang++):
// Compiles and runs successfully, returns 0
//
// Actual behavior (with FlashCpp):
// Crashes during code generation with:
// [ERROR][Codegen] sizeof... operator found during code generation - 
// should have been substituted during template instantiation
//
// Signal: SIGSEGV (Segmentation fault)
//
// Workaround:
// Avoid using sizeof... operator, use recursive template expansion instead
