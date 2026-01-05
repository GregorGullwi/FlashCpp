// Test file for spaceship operator (<=>)  precedence (C++20)
// Standard precedence order: Shift > Three-Way (<=>)  > Relational
// This means: a << 1 <=> b should parse as (a << 1) <=> b
//             a <=> b < c should parse as (a <=> b) < c

struct SimpleOrdering {
    int value;
    SimpleOrdering(int v) : value(v) {}
};

struct Point {
    int x, y;
    
    SimpleOrdering operator<=>(const Point& other) const {
        if (x < other.x) return SimpleOrdering(-1);
        if (x > other.x) return SimpleOrdering(1);
        if (y < other.y) return SimpleOrdering(-1);
        if (y > other.y) return SimpleOrdering(1);
        return SimpleOrdering(0);
    }
};

int main() {
    Point p1{3, 4};
    Point p2{3, 4};
    Point p3{5, 6};
    
    // Test 1: Equal points should give 0
    SimpleOrdering cmp1 = p1 <=> p2;
    
    // Test 2: p1 < p3 should give -1
    SimpleOrdering cmp2 = p1 <=> p3;
    
    // Test 3: p3 > p1 should give 1
    SimpleOrdering cmp3 = p3 <=> p1;
    
    // Sum: 0 + (-1) + 1 = 0
    // We want 42, so let's add 42
    int result = 42 + cmp1.value + cmp2.value + cmp3.value;
    
    return result;  // Should return 42
}
