// Test WITHOUT partial specialization - direct inheritance
// EXPECTED LINK FAIL: Needs copy constructors for base class
template<typename T> 
struct Base { 
    static const int value = 10;
};

template<> 
struct Base<int> { 
    static const int value = 42;
};

// Non-template struct inheriting from template specialization
struct Derived : Base<int> {
};

int main() {
    // This should find value in the base class Base<int>
    // Tests that findStaticMemberRecursive works correctly
    int x = Derived::value;
    return x - 42;  // Should return 0
}
