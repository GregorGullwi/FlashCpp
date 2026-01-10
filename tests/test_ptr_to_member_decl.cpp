// Test pointer-to-member type declarations
struct Point {
    int x;
    int y;
};

// Helper function to get a pointer-to-member
// (Direct initialization with &Point::x currently has parsing limitations)
int Point::*getPtrToX();

int main() {
    Point p = {10, 32};
    int Point::*ptr_to_x = getPtrToX();
    
    // Note: This tests successful parsing of pointer-to-member type declarations
    // The .* operator usage with variables requires additional expression support
    
    return 0;
}
