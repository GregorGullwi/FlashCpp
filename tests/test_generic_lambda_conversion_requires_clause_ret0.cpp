// A generic lambda's requires-clause is evaluated with the template arguments
// deduced from the function-pointer target. A satisfied constraint must not
// block the conversion.

template <class T>
concept WordSized = sizeof(T) == 4;

static int apply(int (*function)(int), int value) {
	return function(value);
}

int main() {
	auto identity = []<class T>(T value) requires WordSized<T> {
		return value;
	};
	return apply(identity, 42) == 42 ? 0 : 1;
}
