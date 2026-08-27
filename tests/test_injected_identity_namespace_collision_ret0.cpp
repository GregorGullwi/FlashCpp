namespace left {
template <class T>
struct Box {
	Box* member = nullptr;
	T value;
	long long namespaceTag = 99;

	Box(T input) : value(input) {}
	Box(const Box& other) : value(other.value), namespaceTag(other.namespaceTag) {}

	Box<T> pass(Box<T> other) {
		Box local(other);
		return local;
	}

	template <class Self = Box>
	int self_size() {
		return sizeof(Self);
	}
};
}

namespace right {
template <class T>
struct Box {
	Box* member = nullptr;
	T value;

	Box(T input) : value(input) {}
	Box(const Box& other) : value(other.value) {}

	Box<T> pass(Box<T> other) {
		Box local(other);
		return local;
	}

	template <class Self = Box>
	int self_size() {
		return sizeof(Self);
	}
};
}

int main() {
	left::Box<int> left_value(17);
	right::Box<int> right_value(25);
	return left_value.pass(left_value).value +
			right_value.pass(right_value).value ==
		42 &&
		left_value.self_size() == sizeof(left::Box<int>) &&
		right_value.self_size() == sizeof(right::Box<int>) &&
		sizeof(left::Box<int>) != sizeof(right::Box<int>)
		? 0
		: 1;
}
