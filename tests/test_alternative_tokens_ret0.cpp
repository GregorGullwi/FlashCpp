// Bug: Alternative operator tokens not recognized by lexer/parser
// Status: COMPILE ERROR - Parser does not recognize bitand, bitor, xor, compl, and, or, not
// Date: 2026-02-07
//
// These are standard C++ alternative representations for operators (ISO 646).
// FlashCpp's lexer does not tokenize them as operators.

int main() {
    int a = 12;  // 1100 in binary
    int b = 10;  // 1010 in binary

    int and_result = a bitand b;  // Should be a & b = 8
    int or_result  = a bitor b;   // Should be a | b = 14
    int xor_result = a xor b;     // Should be a ^ b = 6
    int not_result = compl 0;     // Should be ~0 = -1

    bool x = true;
    bool y = false;
    bool r1 = x and y;   // Should be x && y = false
    bool r2 = x or y;    // Should be x || y = true
    bool r3 = not y;     // Should be !y = true

    return (and_result == 8) && (or_result == 14) && (xor_result == 6) ? 0 : 1;
}

// Expected behavior (with clang++/g++):
// Compiles and runs successfully, returns 0
//
// Actual behavior (with FlashCpp):
// Parse error: "Unknown keyword: b" when encountering "a bitand b"
// The lexer treats "bitand" as an identifier rather than an operator token
//
// Fix: Add alternative token recognition to the lexer. These tokens should
// be treated as aliases: bitand=&, bitor=|, xor=^, compl=~, and=&&, or=||,
// not=!, and_eq=&=, or_eq=|=, xor_eq=^=, not_eq=!=
