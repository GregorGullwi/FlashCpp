// Test requires expression with type requirement including template arguments
// Pattern from <type_traits> line 2736:
// requires requires { typename _Op<_Args...>; }

template<typename T>
struct Container {
    using type = T;
};

// Type requirement with template instantiation in requires expression
template<typename T>
concept HasContainer = requires {
    typename Container<T>;  // Type requirement with template arguments
};

template<HasContainer T>
int getValue() {
    return 42;
}

int main() {
    return getValue<int>();
}
