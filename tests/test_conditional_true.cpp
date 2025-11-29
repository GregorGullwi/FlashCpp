// Test Conditional<true>

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
    int val_true = ct.get_value();
    
    // Should return 100
    return val_true;
}
