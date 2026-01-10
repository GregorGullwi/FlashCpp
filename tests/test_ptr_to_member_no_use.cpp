// Test just the declaration without runtime usage
struct Point {
    int x;
    int y;
};

int Point::*getPtr() {
    return nullptr;
}

int main() {
    Point p = {10, 32};
    int Point::*ptr_to_x = getPtr();
    
    // Don't use .* operator, just test declaration
    return 0;
}
