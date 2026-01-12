// Test direct initialization with variable argument
// Pattern: Type var(other_var); 
// This is common in copy operations

struct Point {
    int x;
    int y;
    Point() : x(0), y(0) {}
    Point(const Point& other) : x(other.x), y(other.y) {}
};

int main() {
    Point a;
    a.x = 20;
    a.y = 22;
    Point b(a);  // Direct initialization with named variable
    return b.x + b.y;
}
