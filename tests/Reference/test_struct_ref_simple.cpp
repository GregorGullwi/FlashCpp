// Simple test to verify struct reference passing
struct Point {
    int x;
    int y;
};

// Pass by const reference
int sumByConstRef(const Point& p) {
    return p.x + p.y;
}

// Pass by reference and modify
void doubleByRef(Point& p) {
    p.x = p.x * 2;
    p.y = p.y * 2;
}

int main() {
    Point p;
    p.x = 10;
    p.y = 20;
    
    // Test const ref
    int sum1 = sumByConstRef(p);  // Should be 30
    
    // Test non-const ref
    doubleByRef(p);  // Should double p to {20, 40}
    int sum2 = sumByConstRef(p);  // Should be 60
    
    return sum1 + sum2;  // 30 + 60 = 90
}
