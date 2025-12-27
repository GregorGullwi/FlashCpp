// Comprehensive auto type deduction test
// Tests various auto features to document what works

struct Point {
    int x;
    int y;
};

Point makePoint(int x, int y) {
    Point p;
    p.x = x;
    p.y = y;
    return p;
}

int main() {
    // Test 1: auto with literals - SHOULD WORK
    auto a = 42;
    
    // Test 2: auto with expressions - SHOULD WORK  
    auto b = a + 10;
    
    // Test 3: auto with function return - SHOULD WORK
    auto p = makePoint(5, 10);
    
    // Test 4: auto& reference - TEST THIS
    int x = 100;
    auto& ref = x;
    ref = 200;
    
    // Test 5: const auto - TEST THIS
    const auto c = 50;
    
    // Test 6: auto* pointer - TEST THIS
    int y = 75;
    int* ptr = &y;
    auto* auto_ptr = ptr;
    
    // Return sum to verify all worked
    return a + (x - 100) + c + *auto_ptr + p.x + p.y;  
    // = 42 + 100 + 50 + 75 + 5 + 10 = 282
}
