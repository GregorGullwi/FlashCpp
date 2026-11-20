// A namespace-scope constexpr pointer initializer resolves unqualified names
// in its declaring namespace and keeps the array source declaration needed for
// later dereference.

namespace N {
constexpr int values[2] = {7, 9};
constexpr const int* pointer = values;
}

constexpr int first = *N::pointer;
static_assert(first == 7, "qualified constexpr pointer dereference");

int main() {
	return first == 7 ? 0 : 1;
}
