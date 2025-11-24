// Test for spaceship operator support with synthesized comparison operators
struct Point {
    int x;
    int y;
    
    // Spaceship operator with default implementation
    auto operator<=>(const Point&) const = default;
};

int main() {
    Point p1{1, 2};
    Point p2{1, 3};
    
    // Test synthesized comparison operators from operator<=>
    bool eq = p1 == p2;  // synthesized from <=>
    bool ne = p1 != p2;  // synthesized from <=>
    bool lt = p1 < p2;   // synthesized from <=>
    bool gt = p1 > p2;   // synthesized from <=>
    bool le = p1 <= p2;  // synthesized from <=>
    bool ge = p1 >= p2;  // synthesized from <=>
    
    return 0;
}
