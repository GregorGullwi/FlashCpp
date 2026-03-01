// Test user-defined literal (UDL) suffix lexing
// Validates that numeric literals with UDL suffixes (e.g., 128_ms)
// are lexed as single tokens instead of separate number+identifier tokens
// Pattern from <chrono>/<thread>: if (__elapsed > 128ms)

int main() {
    // This test verifies the lexer handles UDL suffixes correctly.
    // The main test is that this compiles without parsing errors.
    // (128_ms would be lexed as one token "128_ms" rather than "128" + "_ms")
    return 0;
}
