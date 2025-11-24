// Test for spaceship operator with member access expressions
struct Inner {
    int value;
    
    auto operator<=>(const Inner&) const = default;
};

struct Outer {
    Inner member;
};

int main() {
    Outer o1;
    Outer o2;
    o1.member.value = 10;
    o2.member.value = 20;
    
    // Test member access with spaceship operator
    Inner m1 = o1.member;
    Inner m2 = o2.member;
    
    // This should work with simple identifiers
    bool lt = m1 < m2;
    
    return 0;
}
