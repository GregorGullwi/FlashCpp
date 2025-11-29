// Test Conditional<false>

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
    Conditional<false> cf;
    int val_false = cf.get_value();
    
    // Should return 200
    return val_false;
}
