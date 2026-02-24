template<bool B, typename T = void>
struct enable_if {};

template<typename T>
struct enable_if<true, T> { using type = T; };

template<typename T>
struct is_int { static constexpr bool value = false; };

template<>
struct is_int<int> { static constexpr bool value = true; };

// Overload 1: enabled only for int
template<typename T>
typename enable_if<is_int<T>::value, int>::type
pick(T val) { return val + 100; }

// Overload 2: enabled only for non-int
template<typename T>
typename enable_if<!is_int<T>::value, int>::type
pick(T val) { return 0; }
int main() {
    int a = pick(42);    // calls overload 1 (T=int, enable_if<true>)
    int b = pick(3.14);  // must reject overload 1 via SFINAE, call overload 2
    return (a == 142 && b == 0) ? 0 : 1;
}