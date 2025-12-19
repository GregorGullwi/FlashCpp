// Simplest RVO test for debugging
extern "C" int printf(const char*, ...);

struct Simple {
    int x;
    int y;
    
    Simple(int a, int b) : x(a), y(b) {
        printf("Simple constructor: x=%d, y=%d\n", x, y);
    }
};

Simple makeSimple() {
    printf("In makeSimple, about to return\n");
    return Simple(10, 20);
}

int main() {
    printf("Starting test\n");
    Simple s = makeSimple();
    printf("Got result: x=%d, y=%d\n", s.x, s.y);
    
    if (s.x == 10 && s.y == 20) {
        printf("TEST PASSED\n");
        return 0;
    }
    printf("TEST FAILED\n");
    return 1;
}
