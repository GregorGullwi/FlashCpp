// Test function pointer member WITHOUT initialization

struct Test {
    int (*func)();
};

int main() {
    return 0;
}
