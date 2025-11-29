// Test passing integers through a function to printf

extern "C" int printf(const char*, ...);

void print_one(int a) {
    printf("Value: %d\n", a);
}

int main() {
    printf("Direct call from main: %d\n", 42);
    print_one(42);
    return 0;
}
