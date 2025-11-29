// Exact copy of original test structure

template<int N>
struct Container {
    int data[N];
    
    int get_size() {
        return N;
    }
    
    int double_size() {
        return N * 2;
    }
    
    bool is_large() {
        return N > 10;
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
        if (B) {
            return 100;
        } else {
            return 200;
        }
    }
};

int main() {
    Container<5> c;
    int size = c.get_size();
    int doubled = c.double_size();
    bool large = c.is_large();
    
    Math<10, 3> m;
    int sum = m.add();
    int diff = m.subtract();
    int prod = m.multiply();
    bool gt = m.greater();
    
    Conditional<true> ct;
    Conditional<false> cf;
    int val_true = ct.get_value();
    int val_false = cf.get_value();
    
    // Compute parts separately
    int part1 = size + doubled;  // 5 + 10 = 15
    int part2 = large ? 1 : 0;   // 0
    int part3 = sum + diff + prod;  // 13 + 7 + 30 = 50
    int part4 = gt ? 1 : 0;  // 1
    int part5 = val_true + val_false;  // 100 + 200 = 300
    
    // Total: 15 + 0 + 50 + 1 + 300 = 366
    return part1 + part2 + part3 + part4 + part5;
}
