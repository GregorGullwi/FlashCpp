// Just test Conditional values

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
    
    // Expected: 100 + 200 = 300
    return val_true + val_false;
}
