// Test constexpr virtual destructors

struct Base {
    int value;
    
    constexpr Base(int v) : value(v) {}
    constexpr virtual ~Base() {}
};

struct Derived : Base {
    int extra;
    
    constexpr Derived(int v, int e) : Base(v), extra(e) {}
    constexpr ~Derived() override {}
};

constexpr int test_virtual_dtor() {
    Derived d(10, 20);
    return d.value + d.extra;
}

static_assert(test_virtual_dtor() == 30, "Should return 30");

int main() {
    return 0;
}
