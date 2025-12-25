// Test for ::template keyword in dependent qualified identifiers
// This syntax is needed when accessing member templates in dependent contexts

template<typename T>
struct Base {
    template<typename U>
    struct Inner {
        static constexpr int value = 42;
    };
};

// Using ::template keyword to access dependent member template
template<typename T>
struct Derived {
    using type = typename Base<T>::template Inner<int>;
};

int main() {
    // Test that the dependent type alias works
    return Derived<double>::type::value;  // Should return 42
}
