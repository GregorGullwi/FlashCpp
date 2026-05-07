template <typename T>
struct LateValueProvider;

template <typename T>
auto getLateValue() {
	if constexpr (sizeof(T) == 1) {
		return 0;
	}
	return LateValueProvider<T>::value();
}

template <typename T>
struct LateValueProvider {
	static int value() { return 42; }
};

int main() {
	return getLateValue<int>() - 42;
}
