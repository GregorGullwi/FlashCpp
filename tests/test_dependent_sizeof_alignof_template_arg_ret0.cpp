template <unsigned long long N>
struct SizeValue {
	using type = char[(int)N];
};

template <unsigned long long Alignment>
struct AlignValue {
	struct alignas((size_t)Alignment) type {
		char value;
	};
};

template <typename T>
struct Holder {
	using size_type = typename SizeValue<sizeof(T)>::type;
	using align_type = typename AlignValue<alignof(T)>::type;

	static int get() {
		return (int)sizeof(size_type) + (int)alignof(align_type) * 10;
	}
};

int main() {
	int result = 0;

	if (Holder<int>::get() != ((int)sizeof(int) + (int)alignof(int) * 10)) {
		result |= 1;
	}
	if (Holder<long long>::get() != ((int)sizeof(long long) + (int)alignof(long long) * 10)) {
		result |= 2;
	}

	return result;
}
