// Test: function pointer type alias with variadic parameters
// Verifies that using Alias = void(*)(_Args...); is parsed
// This pattern appears in variant:867 for element_type

template<typename... Args>
struct Holder {
    using func_type = void(*)(Args...);
};

int main() {
    return 0;
}
