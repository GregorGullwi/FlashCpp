// Test that #include_next with a missing file should fail compilation
// This test is expected to fail at compile time with an error message
#include_next <nonexistent_header_that_does_not_exist.h>

int main() {
    return 0;
}
