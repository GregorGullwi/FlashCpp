// Reduced non-std regression for metadata-preserving reconstruction of
// resolved calls and calls with qualified dependent receivers.

int add_values(int left, int right) {
	return left + right;
}

template <typename T>
struct Box {
	T value;

	Box(T initial) : value(initial) {}

	T add(T other) const {
		return value + other;
	}
};

template <typename T>
T invoke_metadata_path(T first, T second) {
	Box<T> box(first);
	return add_values(box.add(second), 0);
}

int main() {
	return invoke_metadata_path(20, 22);
}
