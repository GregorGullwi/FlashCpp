// Test designated initializer with explicit type as function argument
struct Point {
    int x;
    int y;
};

int getX(Point p) {
    return p.x;
}

int getY(Point p) {
    return p.y;
}

int main() {
    // Explicit type with designated init as function argument
    int x = getX(Point{.x = 42, .y = 10});
    int y = getY(Point{.x = 5, .y = 7});

    return x + y - 7;  // 42 + 7 - 7 = 42
}
