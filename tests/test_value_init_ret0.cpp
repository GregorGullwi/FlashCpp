// Test functional value initialization: Type()
// This tests the ability to use Type() syntax to create zero-initialized values
// for all builtin types, including the new C++11/17 char types.

int main() {
    // Basic types
    char c = char();
    int i = int();
    float f = float();
    double d = double();
    short s = short();
    long l = long();
    
    // C++11/C++17 char types
    char8_t c8 = char8_t();
    char16_t c16 = char16_t();
    char32_t c32 = char32_t();
    wchar_t wc = wchar_t();
    
    // All should be zero-initialized
    if (c != 0) return 1;
    if (i != 0) return 2;
    if (f != 0.0f) return 3;
    if (d != 0.0) return 4;
    if (s != 0) return 5;
    if (l != 0) return 6;
    if (c8 != 0) return 7;
    if (c16 != 0) return 8;
    if (c32 != 0) return 9;
    if (wc != 0) return 10;
    
    return 0;
}
