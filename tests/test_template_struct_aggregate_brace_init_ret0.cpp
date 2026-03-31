// Test aggregate brace initialization of template structs with array members.
// Exercises resolve_array_dimensions and is_array_member metadata propagation
// through template instantiation (not covered by the non-template test).

template <int N>
struct ArrayWrapper {
	int arr[N];
	int tail;
};

int main() {
	ArrayWrapper<3> a = {10, 20, 30, 99};

	return (a.arr[0] == 10 &&
			a.arr[2] == 30 &&
			a.tail == 99)
			   ? 0
			   : 1;
}
