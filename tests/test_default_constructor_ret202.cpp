// Test default constructor generation with value initialization

struct Point {
    int x;
    int y;
    Point() : x(100), y(102) {}
};

int main() {
    Point p;
    return p.x + p.y;  // Should return 202
}

