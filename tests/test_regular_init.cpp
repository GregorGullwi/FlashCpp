// Regular initializer test

struct Point {
    int x;
    int y;
};

int test_regular_init() {
    Point p = {10, 20};
    return p.x + p.y;
}

