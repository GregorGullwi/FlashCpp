// Test printf with integer arguments

extern "C" int printf(const char*, ...);

int main() {
    printf("Just a string\n");
    printf("One int: %d\n", 42);
    return 0;
}
