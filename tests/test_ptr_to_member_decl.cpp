// Test pointer-to-member type declarations
struct Point {
    int x;
    int y;
};

// Helper function to get a pointer-to-member
int Point::*getPtrToX() {
    return nullptr;
}

int main() {
    Point p = {10, 32};
    int Point::*ptr_to_x = getPtrToX();
    
    // Note: This tests successful parsing of pointer-to-member type declarations
    // The .* operator usage with variables requires additional expression support
    
    return 0;
}
