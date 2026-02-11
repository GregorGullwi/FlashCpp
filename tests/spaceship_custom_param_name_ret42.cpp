// Test that synthesized comparison operators use the actual parameter name
// from the operator<=> declaration instead of hardcoded "other"
struct Point {
    int x, y;
    auto operator<=>(const Point& rhs) const = default;
};

int main() {
    Point p1{1, 2};
    Point p2{1, 3};
    
    // Test synthesized comparison operators
    // These should use 'rhs' as the parameter name, not 'other'
    bool eq = p1 == p2;  // false
    bool ne = p1 != p2;  // true
    bool lt = p1 < p2;   // true
    bool gt = p1 > p2;   // false
    bool le = p1 <= p2;  // true
    bool ge = p1 >= p2;  // false
    
    // eq=0, ne=1, lt=1, gt=0, le=1, ge=0
    // Total: 0 + 1 + 1 + 0 + 1 + 0 = 3
    int result = (eq ? 1 : 0) + (ne ? 1 : 0) + (lt ? 1 : 0) + 
                 (gt ? 1 : 0) + (le ? 1 : 0) + (ge ? 1 : 0);
    
    // p1.x=1,p1.y=2  p2.x=1,p2.y=3
    // eq=false, ne=true, lt=true, gt=false, le=true, ge=false
    // result = 0 + 1 + 1 + 0 + 1 + 0 = 3
    
    // Add additional tests with different parameter names
    Point p3{5, 5};
    Point p4{5, 5};
    bool eq2 = p3 == p4;  // true
    bool lt2 = p3 < p4;   // false
    
    // eq2=1, lt2=0
    // Total: 3 + 1 + 0 = 4
    result += (eq2 ? 1 : 0) + (lt2 ? 1 : 0);
    
    // Return 42 if result is 4, else return 0
    return result == 4 ? 42 : 0;
}
