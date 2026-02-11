// Test: deduction guide with default parameter values
// This pattern appears in stl_vector.h for vector deduction guides
// Verifies that _Allocator = _Allocator() works in deduction guide params

template<typename T>
struct SimpleAlloc {
    using value_type = T;
};

template<typename T, typename Alloc = SimpleAlloc<T>>
struct Container {
    Container(const T*, const T*) {}
    Container(const T*, const T*, Alloc) {}
};

// Deduction guide with default argument
template<typename T, typename Alloc = SimpleAlloc<T>>
Container(const T*, const T*, Alloc = Alloc()) -> Container<T, Alloc>;

int main() {
    int arr[] = {1, 2, 3};
    return 0;
}
