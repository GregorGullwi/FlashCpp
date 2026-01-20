// Test function reference in requires expression
template<typename T>
concept DefaultConstructible = requires (void(&f)(T)) {
    f({});  // Can call function with default-constructed T
};

// Use the concept
template<typename T> requires DefaultConstructible<T>
struct Wrapper {
    T value;
};

int main() {
    // Test function reference in requires
    Wrapper<int> w{42};
    
    return w.value == 42 ? 0 : 1;
}
