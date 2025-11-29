// Test that FlashCpp can compile code using <cstdio> and std::puts
#include <cstdio>

int main() {
    std::puts("Hello from FlashCpp with <cstdio>!");
    std::puts("Template support is working!");
    return 0;
}
