// Test: Trailing requires clause on member functions
// Verifies that the parser skips requires clauses after noexcept
// and handles requires requires { } compound expressions.

template<typename T>
concept HasSize = requires(T t) { t.size(); };

template<typename T>
struct Wrapper {
    T data;
    
    constexpr int get_size() const noexcept
    requires HasSize<T>
    { return 0; }
    
    constexpr bool is_valid() const noexcept
    requires requires { typename T::value_type; }
    { return true; }
};

int main() {
    return 0;
}
