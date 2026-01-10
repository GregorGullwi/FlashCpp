// Test pointer-to-member operators .* and ->*
struct Point {
    int x;
    int y;
};

int main() {
    Point p = {10, 32};
    int Point::*ptr_to_x = &Point::x;
    
    // Access through .* operator
    int val = p.*ptr_to_x;
    
    return val;  // Should return 10
}
