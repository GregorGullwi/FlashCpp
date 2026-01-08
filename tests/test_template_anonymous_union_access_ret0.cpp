// Test case: Accessing anonymous union members in templates
// Status: ✅ PASSES - Fixed by implementing proper anonymous union member flattening
// Anonymous union members are now properly added to the parent struct member lookup

template<typename T>
struct Container {
    union {
        char dummy;
        T value;
    };
    
    T& get() { 
        return value;  // This now works correctly
    }
};

int main() {
    Container<int> c;
    return 0;
}
