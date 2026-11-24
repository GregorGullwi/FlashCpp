// [expr.sizeof]/2: sizeof(int[]) is ill-formed because the array has an
// unknown bound. An explicit int[] argument must not be canonicalized from the
// placeholder's default builtin category into sizeof(int).
template <typename T>
constexpr int size_of() { return static_cast<int>(sizeof(T)); }

int value = size_of<int[]>();

int main() {
	return value;
}
