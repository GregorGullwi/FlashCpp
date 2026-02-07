// Bug: Digit separators compile but produce incorrect values at runtime
// Status: RUNTIME FAILURE - Compiles and links but values are wrong
// Date: 2026-02-07
//
// Digit separators (') are a C++14 feature: 1'000'000 should equal 1000000.

int main() {
    int large = 1'000'000;         // Should be 1000000
    int hex = 0xFF'FF;             // Should be 65535
    long long big = 1'000'000'000; // Should be 1000000000

    return (large == 1000000) && (hex == 65535) && (big == 1000000000) ? 0 : 1;
}

// Expected behavior (with clang++/g++):
// Compiles and runs successfully, returns 0
//
// Actual behavior (with FlashCpp):
// Compiles and links without errors, but returns 1 at runtime.
// The digit separator (') is likely not being stripped correctly during
// numeric literal parsing, resulting in incorrect integer values.
//
// Fix: Ensure the lexer strips all ' characters from numeric literals
// before converting to integer/float values.
