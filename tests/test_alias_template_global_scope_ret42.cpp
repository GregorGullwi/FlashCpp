// Test top-level template alias with global-scope qualified alias template target
template<bool B, typename T = void>
struct enable_if { using type = T; };

template<typename T>
struct enable_if<false, T> {};

template<bool B, typename T = void>
using enable_if_t = typename enable_if<B, T>::type;

// Global-scope qualified alias target: ::enable_if_t<...>
template<typename T>
using global_cond_t = ::enable_if_t<(sizeof(T) >= 4), int>;

int main() {
    global_cond_t<int> x = 42;
    return x;
}
