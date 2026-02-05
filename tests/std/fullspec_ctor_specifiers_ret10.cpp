// Test: Constructor detection in full template specializations
// Tests that constructors with specifiers (constexpr, explicit) are
// properly recognized inside template<> struct specializations.
// Expected return: 10

template <typename T = void>
struct Handle;

template <>
struct Handle<void> {
    int value;
    constexpr Handle() : value(0) {}
    constexpr explicit Handle(int v) : value(v) {}
};

template <typename T>
struct Handle {
    int value;
    constexpr Handle() : value(0) {}
    constexpr Handle(int v) : value(v) {}
};

int main() {
    Handle<void> h1;
    Handle<void> h2(10);
    return h2.value;
}
