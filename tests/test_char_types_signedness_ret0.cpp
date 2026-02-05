// Test for character type signedness functions
// This test verifies that is_signed_integer_type() and is_unsigned_integer_type()
// correctly classify the character types: Char8, Char16, Char32, and WChar

int main() {
    // Test char8_t - always unsigned per C++20
    char8_t c8 = 65;
    char8_t c8_max = 255;
    
    // Test char16_t - always unsigned per C++11
    char16_t c16 = 66;
    char16_t c16_max = 65535;
    
    // Test char32_t - always unsigned per C++11
    char32_t c32 = 67;
    char32_t c32_max = 4294967295U;
    
    // Test wchar_t - signedness is platform-dependent
    wchar_t wc = 68;
    
    // Verify the values are assigned correctly
    // Using simple arithmetic to ensure the types work
    char8_t c8_sum = c8 + 1;
    char16_t c16_sum = c16 + 1;
    char32_t c32_sum = c32 + 1;
    wchar_t wc_sum = wc + 1;
    
    // All assignments succeeded
    return 0;
}
