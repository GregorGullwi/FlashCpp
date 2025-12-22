// Test constructor with no parameters

struct Point {
    int x;
    int y;
    
    Point() {
        x = 10;
        y = 20;
    }
};

int main() {
    Point p{};
    return p.x + p.y;  // Should return 30
}

