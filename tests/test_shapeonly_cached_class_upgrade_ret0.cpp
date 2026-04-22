// Regression: a class template instantiated during declaration-time default-template
// argument parsing enters ShapeOnly mode first, but a later hard-use of the same
// specialization must still upgrade to full materialization so member bodies exist.

template <typename T>
struct Box {
	T value;

	void set(T next) {
		value = next;
	}
};

template <typename = decltype(Box<int>{})>
struct Trigger {
};

int main() {
	Box<int> box{0};
	box.set(42);
	return box.value - 42;
}
