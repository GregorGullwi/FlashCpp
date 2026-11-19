// Deduction from the target makes T double, which does not satisfy the
// lambda's WordSized constraint. The conversion is not viable.

template <class T>
concept WordSized = sizeof(T) == 4;

static int apply(int (*function)(double), double value) {
	return function(value);
}

int main() {
	auto identity = []<class T>(T value) requires WordSized<T> {
		return value;
	};
	return apply(identity, 42.0);
}
