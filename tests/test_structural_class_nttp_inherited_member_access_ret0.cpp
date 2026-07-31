struct Base {
	int value;
	constexpr Base(int v) : value(v) {}
};

struct Derived : Base {
	constexpr Derived(int v) : Base(v) {}
};

constexpr Derived key{42};

template <Derived K>
constexpr int read_value() {
	return K.value == 42 ? 0 : 1;
}

int main() {
	return read_value<key>();
}
