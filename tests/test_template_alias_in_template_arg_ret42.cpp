// Test: Template alias used as template argument
// This tests that template aliases like __enable_if_t can be used in template argument contexts
// without causing "Missing identifier" errors.

namespace std {

template<bool _Cond, typename _Tp = void>
struct enable_if { };

template<typename _Tp>
struct enable_if<true, _Tp> {
    using type = _Tp;
};

// Template alias
template<bool _Cond, typename _Tp = void>
using __enable_if_t = typename enable_if<_Cond, _Tp>::type;

// Template that uses __enable_if_t in its template argument list
template<typename _Tp, typename...>
using __first_t = _Tp;

namespace __detail {
    // Function template with trailing return type using template aliases
    // This is a simplified version of the pattern from <type_traits>
    template<typename... _Bn>
    auto __or_fn(int) -> __first_t<int, __enable_if_t<true>...>;
    
    template<typename... _Bn>
    auto __or_fn(...) -> int;
}

}

int main() {
    return 42;
}
