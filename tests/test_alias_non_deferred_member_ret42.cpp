// Regression test: alias template resolving to non-deferred type with ::member access
// Previously, this would emit an Error-level "DBG alias instantiated_type return before trailing member"
// diagnostic during normal alias template resolution. Fixed by lowering to Debug level.

template <typename T>
struct Wrapper {
    using value_type = T;
    T data;
    Wrapper(T v) : data(v) {}
};

// Non-deferred alias template (direct substitution)
template <typename T>
using WrapT = Wrapper<T>;

// Access ::value_type on the resolved alias
template <typename T>
WrapT<T>::value_type get_value(const WrapT<T>& w) {
    return w.data;
}

int main() {
    WrapT<int> w(42);
    int v = get_value(w);
    return v;
}
