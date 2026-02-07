// Test if constexpr with sizeof... for compile-time branch elimination in variadic templates
// C++17 if constexpr evaluates condition at compile time and discards the false branch

template<typename T, typename... Rest>
T variadic_sum(T first, Rest... rest) {
    if constexpr (sizeof...(rest) == 0) {
        return first;
    } else {
        return first + variadic_sum(rest...);
    }
}

int main() {
    int result = variadic_sum(1, 2, 3, 4);
    return result == 10 ? 0 : 1;
}
