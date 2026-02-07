// Test variadic template recursion with parameter pack expansion
// Recursive variadic templates using separate overloads for base case and recursive case.

template<typename T>
T var_sum(T val) {
    return val;
}

template<typename T, typename... Rest>
T var_sum(T first, Rest... rest) {
    return first + var_sum(rest...);
}

int main() {
    int result = var_sum(1, 2, 3, 4);
    return result == 10 ? 0 : 1;
}
