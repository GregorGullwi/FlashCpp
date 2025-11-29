// Simple test for pack expansion in function calls

extern "C" int printf(const char*, ...);

void consume(int a, double b, char c) {
    printf("consume(%d, %f, %c)\n", a, b, c);
}

// Template function with pack expansion in call
template<typename... Args>
void forward_all(Args&&... args) {
    // Pack expansion: args... expands to args_0, args_1, args_2
    consume(args...);
}

int main() {
    forward_all(42, 3.14, 'x');
    return 0;
}
