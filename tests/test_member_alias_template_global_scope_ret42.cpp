// Test member template alias whose target is a globally-qualified alias template
template<bool B, typename T = void>
struct enable_if { using type = T; };

template<typename T>
struct enable_if<false, T> {};

template<bool B, typename T = void>
using enable_if_t = typename enable_if<B, T>::type;

struct Checker {
    // Member template alias using global-scope qualified target
    template<typename T>
    using cond_t = ::enable_if_t<(sizeof(T) >= 4), int>;
};

int main() {
    Checker::cond_t<int> x = 42;
    return x;
}
