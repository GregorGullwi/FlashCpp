// Test .* in a standalone statement
struct Point {
    int x;
};

int main() {
    Point p = {10};
    int Point::*ptr = nullptr;
    
    // Try just the expression without assignment
    p.*ptr;
    
    return 0;
}
