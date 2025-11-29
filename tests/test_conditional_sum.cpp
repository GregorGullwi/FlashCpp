// Test addition order

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
    Conditional<true> ct;
    Conditional<false> cf;
    int val_true = ct.get_value();
    int val_false = cf.get_value();
    
    // Should return 100 + 200 = 300
    int sum = val_true + val_false;
    return sum;
}
