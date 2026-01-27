// Test: Struct with lvalue reference member
// This tests a common pattern in standard library (like std::reference_wrapper)
// Reference members are now fully supported in FlashCpp

struct RefHolder {
    int& ref;
    
    // Constructor with member initializer list
    RefHolder(int& r) : ref(r) {}
};

int main() {
    int x = 42;
    RefHolder holder(x);
    
    // Modify through reference
    holder.ref = 100;
    
    // Verify x was modified
    if (x != 100) return 1;
    
    return 0;
}

