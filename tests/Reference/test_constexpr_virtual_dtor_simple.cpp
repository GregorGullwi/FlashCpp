// Test constexpr virtual destructors (without inheritance for MVP)

struct Counter {
    int value;
    
    constexpr Counter(int v) : value(v) {}
    constexpr virtual ~Counter() {}
};

constexpr int test_virtual_dtor() {
    Counter c(42);
    return c.value;
}

static_assert(test_virtual_dtor() == 42, "Should return 42");

int main() {
    return 0;
}
