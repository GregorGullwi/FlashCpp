struct Wrapped;
Wrapped* selectFallback();

struct Wrapped {
	int value;

	Wrapped* operator&() {
		return selectFallback();
	}
};

Wrapped fallback{5};

Wrapped* selectFallback() {
	return __builtin_addressof(fallback);
}

int readValue(Wrapped* ptr) {
	return ptr->value;
}

template <typename T>
struct Holder {
	T value;

	int run() {
		return readValue(__builtin_addressof(value));
	}
};

int add(int a, int b) {
	return a + b;
}

template <typename... Args>
int sumActualValues(Args&... args) {
	return add(readValue(__builtin_addressof(args))...);
}

int main() {
	Holder<Wrapped> holder{{42}};
	if (holder.run() != 42)
		return 1;

	Wrapped a{20};
	Wrapped b{22};
	if (sumActualValues(a, b) != 42)
		return 2;

	return 0;
}
