// Dependent non-type specialization arguments must retain their expression
// identity while a published primary class template body is parsed.
template <int N>
struct Value {
	static constexpr int value = N;
};

template <int N>
struct Forward {
	using exact = Value<N>;
	using shifted = Value<N + 1>;
};

int main() {
	return Forward<41>::exact::value + Forward<41>::shifted::value - 83;
}
