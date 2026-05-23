// Test C++20 != equality rewrite: a != b -> !(a == b)
// struct with only operator== defined, no operator!=
// C++20 should synthesize != from ==
struct MyOrd {
    int _val;
    explicit constexpr MyOrd(int v) : _val(v) {}
    
    // Only operator==, no operator!=
    friend constexpr bool operator==(MyOrd v, int rhs) {
        return v._val == rhs;
    }
};

bool is_neq_test(MyOrd cmp) {
    return cmp != 0;  // C++20 rewrite: !(cmp == 0)
}

int main() {
    MyOrd less = MyOrd(-1);
    MyOrd eq = MyOrd(0);
    
    if (!is_neq_test(less)) return 1;
    if (is_neq_test(eq)) return 2;
    return 0;
}
