// Test pointer-to-member type declarations
struct Point {
    int x;
    int y;
};

int main() {
    Point p = {10, 32};
    int Point::*ptr_to_x = nullptr;
    
    // Note: This tests successful parsing of pointer-to-member type declarations
    // The .* operator usage with variables requires additional expression support
    
    return 0;
}
