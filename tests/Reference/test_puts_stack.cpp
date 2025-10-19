int puts(const char* str);
int printf(const char* format, ...);

int main() {
    // Test 1: Stack-based char array
    char msg[6];
    msg[0] = 'H';
    msg[1] = 'e';
    msg[2] = 'l';
    msg[3] = 'l';
    msg[4] = 'o';
    msg[5] = '\0';
    puts(msg);

    // Test 2: String literal from .rdata section
    puts("World");

    // Test 3: printf with format string and integer argument
    int value = 42;
    printf("Value: %d\n", value);

    // Test 4: printf with multiple arguments
    int x = 10;
    int y = 20;
    int sum = x + y;
    printf("x = %d, y = %d, sum = %d\n", x, y, sum);

    return 0;
}

