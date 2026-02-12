// Test that #include_next works for C wrapper headers
// This tests the preprocessor's ability to handle GCC's #include_next directive
// which is used by C++ standard library headers like <cstdlib>, <cstring>, etc.
#include <cstdlib>
#include <cstring>

int main() {
    // Use functions from the C headers to verify they were properly included
    const char* src = "hello";
    char dst[16];
    memcpy(dst, src, 6);
    if (strcmp(dst, "hello") == 0) return 0;
    return 1;
}
