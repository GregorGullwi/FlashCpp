// Test wmemchr function with C++ overloads from wchar.h
// This test verifies that __GNUC__ defines allow wchar.h to expose C++ overloads

#include <wchar.h>

int main() {
    // Test const overload
    const wchar_t* const_str = L"Hello";
    const wchar_t* const_result = wmemchr(const_str, L'e', 5);
    
    // Test non-const overload  
    wchar_t str[] = L"World";
    wchar_t* result = wmemchr(str, L'o', 5);
    
    // If both overloads are available, this should compile and return 0
    return (const_result != nullptr && result != nullptr) ? 0 : 1;
}
