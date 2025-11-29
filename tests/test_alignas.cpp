// Test alignas keyword

// Test 1: alignas on struct (16-byte alignment)
struct alignas(16) Aligned16 {
    char c;
    int i;
};  // Should be 16 bytes total (natural would be 8)

// Test 2: alignas on struct (32-byte alignment)
struct alignas(32) Aligned32 {
    char c;
};  // Should be 32 bytes total (natural would be 1)

// Test 3: Normal struct for comparison
struct Normal {
    char c;
    int i;
};  // Should be 8 bytes (natural alignment)

int test() {
    // Verify compilation works with all three struct types
    Aligned16 a16;
    Aligned32 a32;
    Normal n;

    return 42;
}

