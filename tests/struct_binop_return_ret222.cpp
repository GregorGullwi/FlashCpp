// Test edge cases for struct return sizes with binary operator overloads
// Tests the fix for TypeSpecifierNode.size_in_bits() returning 0 for structs

// 1. Very small struct (4 bytes = 32 bits) - should return in RAX
struct Tiny {
    int value;
    Tiny(int v) : value(v) {}
    Tiny operator+(const Tiny& other) const {
        return Tiny(value + other.value);
    }
};

// 2. Exactly 8 bytes (64 bits) - should return in RAX on both platforms
struct Small {
    int a, b;
    Small(int x, int y) : a(x), b(y) {}
    Small operator-(const Small& other) const {
        return Small(a - other.a, b - other.b);
    }
};

// 3. 12 bytes (96 bits) - should return in RAX/RDX on Linux
struct Medium {
    int a, b, c;
    Medium(int x, int y, int z) : a(x), b(y), c(z) {}
    Medium operator*(int scalar) const {
        return Medium(a * scalar, b * scalar, c * scalar);
    }
};

int main() {
    // Test tiny (32 bits)
    Tiny t1(10);
    Tiny t2(20);
    Tiny tr = t1 + t2;  // 30
    
    // Test small (64 bits)
    Small s1(100, 200);
    Small s2(90, 30);
    Small sr = s1 - s2;  // (10, 170)
    
    // Test medium (96 bits)
    Medium m1(1, 2, 3);
    Medium mr = m1 * 2;  // (2, 4, 6)
    
    // Return sum
    return tr.value + sr.a + sr.b + mr.a + mr.b + mr.c;
    // 30 + 10 + 170 + 2 + 4 + 6 = 222
}
