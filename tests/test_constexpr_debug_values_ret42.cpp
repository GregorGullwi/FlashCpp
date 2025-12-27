// Debug: What value does the function return?
constexpr int debug_if(int a) {
    if (a > 5) {
        return 99;  // Return a distinctive value
    }
    return 11;  // Return another distinctive value
}

// Test with a=10, should return 99
static_assert(debug_if(10) == 99, "debug_if(10) should be 99");

// Test with a=3, should return 11  
static_assert(debug_if(3) == 11, "debug_if(3) should be 11");

int main() {
    return 42;
}
