// Test nested template base class in partial specialization
// This pattern is used in <type_traits> at line 2422 for common_type

template<typename...>
struct common_type { };

template<typename T, typename U>
struct common_type<T, U> {
    using type = T;
};

template<typename...>
struct pack { };

template<typename, typename, typename = void>
struct fold { };

// Partial specialization with nested template base class
// The base class uses templates instantiated with dependent template parameters
template<typename Tp1, typename Tp2, typename... Rp>
struct common_type<Tp1, Tp2, Rp...>
    : public fold<common_type<Tp1, Tp2>, pack<Rp...>>
{ };

int main() {
    return 0;
}
