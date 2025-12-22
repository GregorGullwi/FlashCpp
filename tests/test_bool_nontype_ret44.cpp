// Test bool non-type template parameter in if condition

template<bool B>
struct Test {
    int get_value() {
        if (B) {
            return 100;
        } else {
            return 200;
        }
    }
};

int main() {
    Test<true> t1;
    Test<false> t2;
    int v1 = t1.get_value();  // Should return 100
    int v2 = t2.get_value();  // Should return 200
    
    // Expected: 100 + 200 = 300
    return v1 + v2;
}
