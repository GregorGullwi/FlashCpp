// Test constinit variables

// constinit implies static storage duration, so these are valid at global scope
constinit int g_a = 10;

// constinit requires a constant initializer
constexpr int const_val = 100;
constinit int g_c = const_val;

void test() {
    // constinit on local static variable
    static constinit int local_static = 42;
    
    // constinit on local variable (should fail compilation if uncommented)
    // constinit int local_fail = 10; 
}

// constinit with non-constant initializer (should fail compilation if uncommented)
// int runtime_val = 5;
// constinit int fail_init = runtime_val;

int main() {
    test();
    return g_a + g_c; // Should return 110
}
