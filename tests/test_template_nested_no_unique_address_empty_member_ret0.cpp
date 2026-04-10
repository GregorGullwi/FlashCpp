struct Empty {};

template<typename T>
struct Outer {
	struct Inner {
		[[no_unique_address]] Empty e;
		T value;
	};
};

static_assert(sizeof(Outer<int>::Inner) == sizeof(int));

int main() {
	return sizeof(Outer<int>::Inner) == sizeof(int) ? 0 : 1;
}
