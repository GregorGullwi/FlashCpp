// A qualified reference to a namespace-scope array keeps the array's source
// declaration, so initializing a pointer from it performs array-to-pointer
// decay instead of evaluating to a null or non-pointer value.

namespace A {
constexpr int values[2] = {7, 9};
}

namespace B {
constexpr const int* pointer = A::values;
}

static_assert(*B::pointer == 7, "qualified array decays to a pointer");
static_assert(B::pointer[1] == 9, "decayed pointer keeps the element address");

int main() {
	return 0;
}
