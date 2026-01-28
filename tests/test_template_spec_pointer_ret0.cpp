// Test template partial specialization for pointers
// Verifies that T* specialization works correctly

template<typename T>
struct TypeInfo {
    static constexpr int is_pointer = 0;
    static constexpr int type_id = 1;
};

// Partial specialization for pointer types
template<typename T>
struct TypeInfo<T*> {
    static constexpr int is_pointer = 1;
    static constexpr int type_id = 2;
};

int main() {
    // Primary template for int
    int a = TypeInfo<int>::is_pointer;      // Should be 0
    int b = TypeInfo<int>::type_id;         // Should be 1
    
    // Pointer specialization for int*
    int c = TypeInfo<int*>::is_pointer;     // Should be 1
    int d = TypeInfo<int*>::type_id;        // Should be 2
    
    // Verify values
    if (a != 0) return 1;
    if (b != 1) return 2;
    if (c != 1) return 3;
    if (d != 2) return 4;
    
    return 0;
}
