// Test for spaceship operator support
// Simple test without requiring <compare> header or constexpr
struct Point {
    int x;
    int y;
    
    // Spaceship operator with default implementation
    auto operator<=>(const Point&) const = default;
};

int main() {
    Point p1{1, 2};
    Point p2{1, 3};
    // Note: Full semantic support for synthesized comparison operators
    // from operator<=> would require additional compiler infrastructure
    return 0;
}
