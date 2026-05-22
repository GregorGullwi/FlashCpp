// Regression: out-of-line constructor templates on class templates must
// replay base/member initializers and preserve definition-time lookup.

int helperValue(int value) {
	return value + 1;
}

template <typename T>
struct Base {
	int base;

	Base(int value) : base(value) {}
};

template <typename T>
struct Box : Base<T> {
	int member;

	template <typename U>
	Box(U value);

	int total() const {
		return this->base + member;
	}
};

template <typename T>
template <typename U>
Box<T>::Box(U value)
	: Base<T>(helperValue(20)),
	  member(static_cast<int>(value)) {}

int main() {
	Box<int> box(21);
	return box.total();
}
