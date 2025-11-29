// Test non-type template parameters in expressions
// Currently non-type params only work for array sizes
// This test will verify they work in all expression contexts

template<int N>
struct Container {
    int data[N];  // ✅ This already works
    
    int get_size() {
        return N;  // ❌ Need to make this work
    }
    
    int double_size() {
        return N * 2;  // ❌ Arithmetic with N
    }
    
    bool is_large() {
        return N > 10;  // ❌ Comparison with N
    }
};

template<int A, int B>
struct Math {
    int add() { return A + B; }
    int subtract() { return A - B; }
    int multiply() { return A * B; }
    bool greater() { return A > B; }
};

template<bool B>
struct Conditional {
    int get_value() {
        if (B) {  // ❌ Use bool in if condition
            return 100;
        } else {
            return 200;
        }
    }
};

int main() {
    Container<5> c;
    int size = c.get_size();  // Should return 5
    int doubled = c.double_size();  // Should return 10
    bool large = c.is_large();  // Should return false (5 > 10 is false)
    
    Math<10, 3> m;
    int sum = m.add();  // Should return 13
    int diff = m.subtract();  // Should return 7
    int prod = m.multiply();  // Should return 30
    bool gt = m.greater();  // Should return true (10 > 3)
    
    Conditional<true> ct;
    Conditional<false> cf;
    int val_true = ct.get_value();  // Should return 100
    int val_false = cf.get_value();  // Should return 200
    
    // Expected: 5 + 10 + 0 + 13 + 7 + 30 + 1 + 100 + 200 = 366
    return size + doubled + (large ? 1 : 0) + sum + diff + prod + (gt ? 1 : 0) + val_true + val_false;
}
