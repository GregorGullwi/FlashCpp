// Test brace initialization in requires expression simple requirement
// Pattern: _Tp{} - brace initialization of template parameter type

template<typename T>
concept default_constructible = requires {
    T{};  // Creates a default-constructed temporary of type T
};

struct HasDefault {
    int value;
    HasDefault() : value(42) {}
};

int main() {
    if constexpr (default_constructible<HasDefault>) {
        return 0;  // HasDefault is default constructible
    }
    return 1;
}
