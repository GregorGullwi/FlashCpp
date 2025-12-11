// Expected fail test: Struct with lvalue reference member
// This tests a common pattern in standard library (like std::reference_wrapper)
// Currently fails due to reference member initialization limitations

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

/*
 * KNOWN ISSUE:
 * FlashCpp currently cannot handle struct members that are lvalue references.
 * Error: "Reference member initializer must be an lvalue"
 * 
 * This is needed for:
 * - std::reference_wrapper<T>
 * - std::tuple with reference elements
 * - Many standard library containers when holding references
 */
