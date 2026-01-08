// Test case: Accessing anonymous union members in templates
// Status: ✅ PASSES - Fixed by implementing proper anonymous union member flattening
// Anonymous union members are now properly added to the parent struct member lookup

template<typename T>
struct Container {
    union {
        char dummy;
        T value;
    };
};

int main() {
    Container<int> c;
    c.value = 42;  // Initialize the value
    
    // Verify we can read back the value
    if (c.value != 42) return 1;
    
    // Change the value and verify again
    c.value = 100;
    if (c.value != 100) return 2;
    
    return 0;
}
