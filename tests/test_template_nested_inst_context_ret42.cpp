// Test that nested types inside template instantiations carry
// the enclosing template's instantiation context, so constexpr
// evaluation can resolve template bindings without registry lookups.

template <typename T, int N>
struct Holder {
struct Inner {
static constexpr int value = N + sizeof(T);
};
};

template <typename T>
struct Wrapper {
static constexpr int size = sizeof(T);
struct Nested {
static constexpr int inherited_size = sizeof(T);
};
};

int main() {
// Test 1: nested struct with non-type template parameter and sizeof(T)
int r1 = Holder<int, 34>::Inner::value;  // 34 + 4 = 38

// Test 2: different instantiation
int r2 = Holder<char, 37>::Inner::value;  // 37 + 1 = 38

// Test 3: outer static constexpr
int r3 = Wrapper<int>::size;  // 4

// Test 4: nested struct inheriting sizeof from outer template
int r4 = Wrapper<int>::Nested::inherited_size;  // 4

// Validate all and produce 42
if (r1 != 38) return 1;
if (r2 != 38) return 2;
if (r3 != 4) return 3;
if (r4 != 4) return 4;
return r1 + r3;  // 38 + 4 = 42
}
