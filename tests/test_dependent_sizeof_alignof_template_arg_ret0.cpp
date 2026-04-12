template <unsigned long long N>
struct SizeValue {
	using type = char[(int)N];
};

template <typename T>
struct Holder {
	using size_type = typename SizeValue<sizeof(T)>::type;

	static int get() {
		return (int)sizeof(size_type);
	}
};

int main() {
	int result = 0;

	if (Holder<int>::get() != (int)sizeof(int)) {
		result |= 1;
	}
	if (Holder<long long>::get() != (int)sizeof(long long)) {
		result |= 2;
	}

	return result;
}
