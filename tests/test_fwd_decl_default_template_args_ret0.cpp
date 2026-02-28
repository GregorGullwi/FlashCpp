// Test: Forward declaration with default template args merged into full definition
// Per C++ [temp.param]/11: defaults appear on the first declaration, not repeated on definition

template<long long N = 1, long long D = 1>
struct ratio {
    static constexpr long long num = N;
    static constexpr long long den = D;
};

namespace chrono {
// Forward declaration with defaults
template<typename _Rep, typename _Period = ratio<1>>
class duration;

// Full definition WITHOUT defaults (standard C++ pattern)
template<typename _Rep, typename _Period>
class duration {
public:
    _Rep __r;
    constexpr duration(_Rep r) : __r(r) {}
};
}

// This uses duration with only 1 template arg, relying on the default from forward decl
constexpr chrono::duration<long double>
make_dur(long double __secs)
{ return chrono::duration<long double>{__secs}; }

int main() {
    auto d = make_dur(3.14L);
    return 0;
}
