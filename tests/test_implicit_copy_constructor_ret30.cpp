// Test implicit (compiler-generated) copy constructor
struct Point {
    int x;
    int y;
    // No user-defined constructors - compiler should generate default and copy constructors
};

int main() {
    Point p1;
    p1.x = 10;
    p1.y = 20;

    // This should use the compiler-generated copy constructor
    Point p2(p1);

    return p2.x + p2.y;  // Should return 30
}

