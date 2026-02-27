// Test: variable template partial specialization with dependent return type
// The declared type T must be substituted along with the initializer
template<typename T>
constexpr T typed_size_v = T{};

template<typename T>
constexpr T typed_size_v<T&> = static_cast<T>(sizeof(T));

int main() {
    static_assert(typed_size_v<int&> == 4, "should be sizeof(int)");
    return typed_size_v<int&>;  // Expected: 4
}
