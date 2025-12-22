// Test constructor with member initializer list

struct Point {
    int x;
    int y;
    
    Point(int a, int b) : x(a), y(b) {}
};

int main() {
    Point p = {10, 20};
    return p.x + p.y;  // Should return 30
}

