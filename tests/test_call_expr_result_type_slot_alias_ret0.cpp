template <typename T>
struct AliasType {
	using Type = T;
};

template <typename T>
typename AliasType<T>::Type increment(T value) {
	return static_cast<typename AliasType<T>::Type>(value + 1);
}

int callFree() {
	return increment<int>(41);
}

struct Accumulator {
	int base;
	using ReturnType = int;

	ReturnType addOne() {
		return increment<int>(base);
	}
};

int main() {
	Accumulator acc{41};
	int direct = callFree();
	int member = acc.addOne();
	return (direct == 42 && member == 42) ? 0 : 1;
}
