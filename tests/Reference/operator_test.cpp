// Test file to verify all multi-character operators are correctly lexed
int test_operators() {
    int a = 10;
    int b = 5;
    int* ptr = &a;
    
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
    
    ptr->a;   // ->
    
    // Three-character operators
    a <<= b;  // <<=
    a >>= b;  // >>=
    
    return a;
}

int main() {
    return test_operators();
}

