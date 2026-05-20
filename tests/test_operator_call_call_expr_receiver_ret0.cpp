template <typename T>
struct Adder {
	T base{};

	T operator()(T value) const {
		return base + value;
	}
};

template <typename T>
Adder<T> makeAdder(T value) {
	Adder<T> out;
	out.base = value;
	return out;
}

int main() {
	const int int_result = makeAdder<int>(40)(2);
	const long long_result = makeAdder<long>(20L)(22L);
	const double double_result = makeAdder<double>(41.5)(0.5);
	return (int_result == 42 && long_result == 42L && double_result == 42.0) ? 0 : 1;
}
