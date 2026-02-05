// Test correct mangling of size_t on both SystemV (Linux) and MSVC (Windows)
// On Linux: size_t = unsigned long (mangled as 'm' in Itanium ABI)
// On Windows: size_t = unsigned __int64 (mangled as '_K' in MSVC ABI)
// This test verifies that size_t is correctly resolved from __SIZE_TYPE__ and
// that "long unsigned int" is parsed correctly as "unsigned long"

#include <cstddef>

// Function taking size_t parameter
void process_size(size_t n) {
    // Just consume the parameter
    (void)n;
}

// Function taking explicit unsigned long parameter
void process_ulong(unsigned long n) {
    (void)n;
}

// Function taking "long unsigned int" (should be same as unsigned long)
void process_long_unsigned_int(long unsigned int n) {
    (void)n;
}

int main() {
    // Test that size_t and unsigned long are compatible
    size_t sz = 42;
    unsigned long ul = 42;
    long unsigned int lui = 42;

    // All three should be the same type
    process_size(sz);
    process_size(ul);    // Should work - same type on Linux
    process_size(lui);   // Should work - same type on Linux

    process_ulong(sz);   // Should work - same type on Linux
    process_ulong(ul);
    process_ulong(lui);

    process_long_unsigned_int(sz);   // Should work - same type on Linux
    process_long_unsigned_int(ul);
    process_long_unsigned_int(lui);

    return 0;
}
