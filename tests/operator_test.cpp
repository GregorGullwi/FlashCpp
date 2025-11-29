// Test file to verify all multi-character operators are correctly lexed

struct Point {
    int x;
    int y;
};

int test_operators() {
    int a = 10;
    int b = 5;
    int* ptr = &a;
    
    Point point = {1, 2};
    Point* ppoint = &point;
    
    // Two-character operators
    a += b;   // +=
    a -= b;   // -=
    a *= b;   // *=
    a /= b;   // /=
    a %= b;   // %=
    a &= b;   // &=
    a |= b;   // |=
    a ^= b;   // ^=
    
    a++;      // ++
    a--;      // --
    
    a << b;   // <<
    a >> b;   // >>
    a <= b;   // <=
    a >= b;   // >=
    a == b;   // ==
    a != b;   // !=
    
    a && b;   // &&
    a || b;   // ||
    
    ppoint->x;   // ->
    
    // Three-character operators
    a <<= b;  // <<=
    a >>= b;  // >>=
    
    return a;
}

int main() {
    return test_operators();
}

