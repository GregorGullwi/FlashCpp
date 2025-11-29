// Test implicit (compiler-generated) operator= (copy assignment operator)
struct Point {
    int x;
    int y;
    // No user-defined operator= - compiler should generate implicit copy assignment operator
};

int main() {
    Point p1;
    p1.x = 10;
    p1.y = 20;
    
    Point p2;
    p2.x = 5;
    p2.y = 15;
    
    // This should use the compiler-generated operator=
    p2 = p1;
    
    return p2.x + p2.y;  // Should return 30
}

