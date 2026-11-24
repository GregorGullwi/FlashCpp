// [dcl.array]/1, [expr.sizeof]/2: long long[][3] is an array of unknown bound
// whose element type is long long[3]. sizeof of the whole array is ill-formed,
// so the function-template body must reject it instead of reporting 24.
template <typename T>
constexpr int size_of() { return static_cast<int>(sizeof(T)); }

int value = size_of<long long[][3]>();

int main() {
	return value;
}
