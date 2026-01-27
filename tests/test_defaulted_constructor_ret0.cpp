// Test = default syntax for constructors
struct Point {
    int x;
    int y;
    
    // Explicitly defaulted default constructor
    Point() = default;
    
    // Explicitly defaulted copy constructor
    Point(const Point& other) = default;
    
    // Explicitly defaulted move constructor
    Point(Point&& other) = default;
    
    // Explicitly defaulted copy assignment operator
    Point& operator=(const Point& other) = default;
    
    // Explicitly defaulted move assignment operator
    Point& operator=(Point&& other) = default;
};

int main() {
    return 0;
}
