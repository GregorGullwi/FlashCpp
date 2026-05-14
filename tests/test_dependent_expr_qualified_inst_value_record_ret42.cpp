// Regression: expression qualified-ids with dependent template-instantiation
// owners preserve their owner arguments for later substitution.

template <typename T>
struct Holder {
	static constexpr int value = T::value;
};

template <typename T>
int read_holder_value() {
	return Holder<T>::value;
}

struct Answer {
	static constexpr int value = 42;
};

int main() {
	return read_holder_value<Answer>();
}
