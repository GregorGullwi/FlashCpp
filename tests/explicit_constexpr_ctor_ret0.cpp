// Test: explicit constexpr constructor in member function template
// Both orderings should work: 'explicit constexpr' and 'constexpr explicit'

template<typename... Conds>
using _Requires = int;

template<typename _Tp>
struct optional {
    template<typename _Up,
             _Requires<_Up> = 0>
    explicit constexpr optional(_Up&& __t) {}
};

int main() {
    optional<int> opt(42);
    return 0;
}
