// Test non-type template default parameter referencing earlier type parameter.

template<typename T>
struct is_int {
    static constexpr bool value = false;
};

template<>
struct is_int<int> {
    static constexpr bool value = true;
};

template<typename T, bool IsInt = is_int<T>::value>
struct marker {
    static constexpr bool value = IsInt;
};

int main() {
    return marker<int>::value ? 42 : 0;
}
