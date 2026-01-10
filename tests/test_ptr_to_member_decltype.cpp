// Test pointer-to-member operator .* in decltype context (type_traits pattern)
// This mimics the pattern from <type_traits> line 2499

template<typename T>
T declval();

struct Functor {
    int operator()(int x) { return x * 2; }
};

// This is the pattern from type_traits that's failing
template<typename Fp, typename Tp1>
using result_t = decltype((declval<Tp1>().*declval<Fp>())(declval<int>()));

int main() {
    // We're just testing parsing, not actually using result_t
    return 0;
}
