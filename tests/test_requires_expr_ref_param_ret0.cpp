// Test requires expression with reference parameter
template<typename T>
concept Destructible = requires (T& t) { t.~T(); };

template<typename T>
  requires Destructible<T>
constexpr bool is_destructible() { return true; }

int main() {
    static_assert(is_destructible<int>(), "int should be destructible");
    return 0;
}
