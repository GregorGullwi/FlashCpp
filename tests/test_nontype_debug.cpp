// Test non-type template parameters - debug version
// Using extern "C" printf declaration instead of <stdio.h>

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

extern "C" int printf(const char*, ...);

int main() {
    Container<5> c;
    int size = c.get_size();
    int doubled = c.double_size();
    bool large = c.is_large();
    
    printf("size = %d (expected 5)\n", size);
    printf("doubled = %d (expected 10)\n", doubled);
    printf("large = %d (expected 0)\n", large ? 1 : 0);
    
    Math<10, 3> m;
    int sum = m.add();
    int diff = m.subtract();
    int prod = m.multiply();
    bool gt = m.greater();
    
    printf("sum = %d (expected 13)\n", sum);
    printf("diff = %d (expected 7)\n", diff);
    printf("prod = %d (expected 30)\n", prod);
    printf("gt = %d (expected 1)\n", gt ? 1 : 0);
    
    Conditional<true> ct;
    Conditional<false> cf;
    int val_true = ct.get_value();
    int val_false = cf.get_value();
    
    printf("val_true = %d (expected 100)\n", val_true);
    printf("val_false = %d (expected 200)\n", val_false);
    
    int total = size + doubled + (large ? 1 : 0) + sum + diff + prod + (gt ? 1 : 0) + val_true + val_false;
    printf("total = %d (expected 366)\n", total);
    
    return total;
}
