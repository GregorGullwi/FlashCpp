struct Empty {};

template<typename T>
struct Holder {
	[[no_unique_address]] Empty e;
	T value;
};

static_assert(sizeof(Holder<int>) == sizeof(int));

int main() {
	return sizeof(Holder<int>) == sizeof(int) ? 0 : 1;
}
