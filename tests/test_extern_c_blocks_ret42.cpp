// Test that FlashCpp can compile code using extern "C" declarations
// This is a simplified version that doesn't require full cstdio includes

// Declare puts in an extern "C" block
extern "C" {
    int puts(const char* str);
}

// Also test namespace with extern "C++" 
namespace std {
    extern "C++" {
        inline int test_func() { return 42; }
    }
}

int main() {
    puts("Hello from FlashCpp with extern C!");
    return std::test_func();
}
