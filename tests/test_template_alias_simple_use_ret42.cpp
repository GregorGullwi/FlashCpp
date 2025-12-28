// Simplest test case for template alias in template argument
template<typename T>
struct identity {
    using type = T;
};

// Template alias
template<typename T>
using identity_t = typename identity<T>::type;

// Try to use template alias as template argument
template<typename T>
struct wrapper {};

int main() {
    wrapper<identity_t<int>> w;
    return 42;
}
