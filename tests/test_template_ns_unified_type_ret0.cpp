// Test: namespace-qualified template instantiation produces a single TypeInfo entry.
// custom_ns::Holder<int> should resolve to the same type whether accessed via
// qualified or unqualified name (no duplicate type entries in gTypesByName).

namespace custom_ns {
template<typename T>
struct Holder {
    T value;
    T get() const { return value; }
};
}

int main() {
    custom_ns::Holder<int> h{42};
    int v = h.get();
    return v == 42 ? 0 : 1;
}
