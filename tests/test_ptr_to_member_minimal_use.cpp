// Minimal test for .* operator with identifier
struct Point {
    int x;
};

int main() {
    Point p = {10};
    int Point::*ptr = nullptr;
    
    // This line causes the hang
    int val = p.*ptr;
    
    return 0;
}
