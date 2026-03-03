// Test that pattern modifier stripping works correctly in deferred body re-parsing.
// For a partial spec with pattern T*, when instantiated with int*,
// T should be deduced as int (not int*).
// The member function uses sizeof(T) to verify correct deduction.

struct Outer {
    template<typename T>
    struct Inner {
        T val;
        int get_size() { return sizeof(T); }
    };

    // Partial specialization for pointers: T* pattern means T = base type
    template<typename T>
    struct Inner<T*> {
        T* val;
        // sizeof(T) should be sizeof(int), not sizeof(int*)
        int get_size() { return sizeof(T); }
    };
};

int main() {
    int x = 42;
    Outer::Inner<int*> ip{&x};
    
    // On a 64-bit platform: sizeof(int) == 4, sizeof(int*) == 8
    // If T is correctly deduced as int, get_size() returns 4
    // If T is incorrectly left as int*, get_size() returns 8
    int sz = ip.get_size();
    
    // Return 0 only if sizeof(T) == sizeof(int) (correct deduction)
    if (sz == sizeof(int)) return 0;
    return 1;
}
