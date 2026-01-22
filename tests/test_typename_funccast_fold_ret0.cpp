// Test typename functional cast in fold expression pattern
// This pattern is used in ext/type_traits.h for __promoted_t
template<typename T>
struct __promote {
    using __type = T;
};

template<typename... _Tp>
using __promoted_t = decltype((typename __promote<_Tp>::__type(0) + ...));

int main() {
    return 0;
}
