// A qualified reference to a namespace-scope array keeps the array's source
// declaration, so initializing a pointer from it performs array-to-pointer
// decay and emits a relocation to the real array storage.

namespace A {
constexpr int values[2] = {7, 9};
}

namespace B {
constexpr const int* pointer = A::values;
}

static_assert(*B::pointer == 7, "qualified array decays to a pointer");
static_assert(B::pointer != nullptr, "decayed pointer is non-null");

int main() {
	return B::pointer[1];
}
