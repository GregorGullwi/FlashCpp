// Test auto return type with noexcept specifier before trailing return type
// Pattern from <type_traits>: auto declval() noexcept -> decltype(...)

template<typename T>
auto get_value() noexcept -> T;

int main() {
    return 0;
}
