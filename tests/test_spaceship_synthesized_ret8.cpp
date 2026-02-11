// Test: synthesized comparison operators from defaulted operator<=>
// C++20: auto operator<=>(const T&) const = default; synthesizes ==, !=, <, >, <=, >=
struct IntWrapper {
    int value;
    auto operator<=>(const IntWrapper&) const = default;
};

int main() {
    IntWrapper a{10};
    IntWrapper b{20};
    IntWrapper c{10};
    
    // Test synthesized == and !=
    int r1 = !(a == b) ? 1 : 0;  // a != b → true → 1
    int r2 = (a == c) ? 1 : 0;   // a == c → true → 1
    int r3 = (a != b) ? 1 : 0;   // a != b → true → 1
    int r4 = !(a != c) ? 1 : 0;  // a == c → true → 1
    
    // Test synthesized relational operators
    int r5 = (a < b) ? 1 : 0;    // 10 < 20 → true → 1
    int r6 = (b > a) ? 1 : 0;    // 20 > 10 → true → 1
    int r7 = (a <= c) ? 1 : 0;   // 10 <= 10 → true → 1
    int r8 = (a >= c) ? 1 : 0;   // 10 >= 10 → true → 1
    
    return r1 + r2 + r3 + r4 + r5 + r6 + r7 + r8;  // Should be 8
}
