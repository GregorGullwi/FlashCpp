struct Point {
    int x = 5;    // Default member initializer
    int y = 10;   // Default member initializer
};

int main() {
    Point p;
    return p.x + p.y;  // Should return 15 (5 + 10)
}
