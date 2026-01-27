// Test float parameter passing through functions

extern "C" int printf(const char*, ...);

void level2(float a, double b) {
    printf("level2: a=%f, b=%f\n", a, b);
}

void level1(float a, double b) {
    printf("level1: a=%f, b=%f\n", a, b);
    level2(a, b);
}

int main() {
    printf("main: calling level1(3.14f, 2.718)\n");
    level1(3.14f, 2.718);
    return 0;
}
