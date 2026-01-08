// Test case: Union containing a struct
// Status: ✅ PASSES - Unions can contain struct members

struct Point {
    int x;
    int y;
};

struct Data {
    union {
        int i;
        float f;
        Point p;
    };
};

int main() {
    Data d;
    
    // Write values through the union
    d.i = 42;
    
    // Note: Unions share memory, so setting p will overwrite i
    d.p.x = 10;
    d.p.y = 20;
    
    // Read back and verify the Point values
    int sum = d.p.x + d.p.y;
    
    // Expected: 10 + 20 = 30
    if (sum != 30) return 1;
    
    return 0;
}
