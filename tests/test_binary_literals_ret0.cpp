// Bug: Binary literals (0b prefix) not recognized by lexer
// Status: COMPILE ERROR - Lexer fails on 0b prefix
// Date: 2026-02-07
//
// Binary literals (0b/0B prefix) are a C++14 feature and must be supported in C++20.

int main() {
    int bin1 = 0b1010;      // Should be 10
    int bin2 = 0b11111111;  // Should be 255
    int bin3 = 0B0;         // Should be 0

    return (bin1 == 10) && (bin2 == 255) && (bin3 == 0) ? 0 : 1;
}

// Expected behavior (with clang++/g++):
// Compiles and runs successfully, returns 0
//
// Actual behavior (with FlashCpp):
// Parse error: "Missing identifier: b1010" - the lexer interprets 0b1010 as
// the integer literal 0 followed by identifier "b1010"
//
// Fix: Extend the numeric literal lexer to recognize the 0b/0B prefix and
// parse subsequent [01]+ digits as a binary integer literal.
