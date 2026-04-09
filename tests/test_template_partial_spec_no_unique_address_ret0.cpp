struct Empty {};

template<typename T>
struct Holder;

template<typename T>
struct Holder<T*> {
	[[no_unique_address]] Empty e;
	int value;
};

static_assert(sizeof(Holder<int*>) == sizeof(int));

int main() {
	return sizeof(Holder<int*>) == sizeof(int) ? 0 : 1;
}
