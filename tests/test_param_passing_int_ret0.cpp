// Test integer parameter passing through functions

extern "C" int printf(const char*, ...);

void level2(int a, int b, int c) {
    printf("level2: a=%d, b=%d, c=%d\n", a, b, c);
}

void level1(int a, int b, int c) {
    printf("level1: a=%d, b=%d, c=%d\n", a, b, c);
    level2(a, b, c);
}

int main() {
    printf("main: calling level1(10, 20, 30)\n");
    level1(10, 20, 30);
    return 0;
}
