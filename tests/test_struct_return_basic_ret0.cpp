struct Point {
    int x;
    int y;
};

Point createPoint() {
    Point p;
    p.x = 5;
    p.y = 10;
    return p;
}

extern "C" int printf(const char*, ...);

int main() {
    printf("Calling createPoint\n");
    Point p = createPoint();
    printf("Got point: x=%d, y=%d\n", p.x, p.y);
    
    if (p.x == 5 && p.y == 10) {
        printf("TEST PASSED\n");
        return 0;
    }
    printf("TEST FAILED\n");
    return 1;
}
