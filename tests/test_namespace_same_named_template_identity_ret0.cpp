// Same-spelled templates in distinct namespaces must retain distinct identities.

namespace left {
template <class T>
struct Box {
	T value;
	long long namespaceTag;
};
}

namespace right {
template <class T>
struct Box {
	T value;
};
}

int main() {
	left::Box<int> leftValue;
	right::Box<int> rightValue;
	leftValue.value = 41;
	rightValue.value = 42;
	return leftValue.value == 41 && rightValue.value == 42 &&
			sizeof(leftValue) != sizeof(rightValue)
		? 0
		: 1;
}
