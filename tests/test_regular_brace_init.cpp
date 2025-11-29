struct Point {
    int x;
    int y;
};

int test_regular_brace_init() {
    Point p = {10, 20};
    return p.x + p.y;
}

