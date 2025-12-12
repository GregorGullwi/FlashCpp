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
    
    // Test partial specialization (should be double*)
    double value = 2.71;
    Wrapper<int, false>::Type<double> y = &value;
    *y = 5.0;
    
    return (*y == 5.0 && value == 5.0) ? 0 : 1;
}
