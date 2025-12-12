// Test if member template aliases work in current branch
// This feature was added in main branch commits a2a564c through d32120a

struct TypeTraits {
    // Simple member template alias
    template<typename T>
    using Ptr = T*;
};

int main() {
    int x = 100;
    
    // Test: Simple pointer alias
    TypeTraits::Ptr<int> p1 = &x;
    int result1 = *p1;  // 100
    
    return result1 - 100;  // Should return 0 if working
}
