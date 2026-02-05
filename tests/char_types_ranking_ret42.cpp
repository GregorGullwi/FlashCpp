// Test for character type rankings in integer conversions
// This test verifies that char8_t, char16_t, char32_t, and wchar_t
// have the correct conversion ranks for integer promotion

int test_char8_conversion() {
    // char8_t should behave like unsigned char (rank 1)
    char8_t c8 = 5;
    return c8 + 10;  // Should promote to int and return 15
}

int test_char16_conversion() {
    // char16_t should have rank 2 (like short)
    char16_t c16 = 10;
    return c16 + 5;  // Should promote to int and return 15
}

int test_char32_conversion() {
    // char32_t should have rank 3 (like int)
    char32_t c32 = 7;
    return c32 + 5;  // Should return 12
}

int test_wchar_conversion() {
    // wchar_t is target-dependent
    wchar_t wc = 0;
    return wc;  // Should return 0
}

int main() {
    // 15 + 15 + 12 + 0 = 42
    return test_char8_conversion() + test_char16_conversion() + test_char32_conversion() + test_wchar_conversion();
}
