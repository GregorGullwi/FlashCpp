// Test: recursive variadic template with base case (classic recursion pattern)
template<typename T>
T recursive_sum(T val) {
return val;
}

template<typename T, typename... Rest>
T recursive_sum(T first, Rest... rest) {
return first + recursive_sum(rest...);
}

int main() {
return recursive_sum(1, 2, 4, 8, 27); // = 42
}
