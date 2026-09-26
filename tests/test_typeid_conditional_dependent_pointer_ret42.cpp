// The structural conditional planner must handle pointer qualification after
// a template parameter has been substituted with its concrete type.
template <typename T>
const T* select_pointer(T* mutable_pointer, const T* const_pointer) {
	return (1 == 1) ? mutable_pointer : const_pointer;
}

int main() {
	int value = 42;
	return *select_pointer(&value, &value);
}
