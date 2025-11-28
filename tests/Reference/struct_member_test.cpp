struct Point {
    int x;
    int y;
};

int main() {
    Point p;
    p.x = 10;
    p.y = 20;
    return p.x + p.y;  // Should return 30
}
