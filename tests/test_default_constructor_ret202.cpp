// Test default constructor generation

struct Point {
    int x;
    int y;
};

int main() {
    Point p;  // Should call default constructor that zero-initializes members
    return p.x + p.y;  // Should return 0
}

