template <class T>
struct Value {
	T value;

	Value(T input) : value(input) {}
	Value(const Value& other) : value(other.value) {}

	Value return_self() {
		return *this;
	}

	int parameter_self(const Value& other) {
		return other.value;
	}
};

int main() {
	Value<int> first(21);
	Value<int> second(first);
	Value<int> third = second.return_self();
	return third.parameter_self(first) + third.value == 42 ? 0 : 1;
}
