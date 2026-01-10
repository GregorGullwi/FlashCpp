// Test runtime pointer-to-member operator usage
struct Point {
    int x;
    int y;
};

int Point::*getPtr() {
    return nullptr;  // Simplified for testing
}

int main() {
    Point p = {10, 32};
    int Point::*ptr_to_x = getPtr();
    
    // Test runtime .* operator usage
    int val = p.*ptr_to_x;
    
    return 0;
}
