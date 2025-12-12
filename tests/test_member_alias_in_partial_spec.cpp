// Test: Check if member template alias works in partial specialization
// This tests if the issue is specific to full specializations only

template<typename T, bool B>
struct Wrapper {
    template<typename U>
    using Type = U;
};

// Partial specialization
template<typename T>
struct Wrapper<T, false> {
    template<typename U>  // Does this parse?
    using Type = U*;
};

int main() {
    // Test primary template
    Wrapper<int, true>::Type<double> x = 3.14;
    
    // Test partial specialization
    Wrapper<int, false>::Type<double> y;
    *y = 2.71;
    
    return (*y == 2) ? 0 : 1;
}
