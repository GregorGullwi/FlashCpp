struct Empty {};

struct A {
	[[no_unique_address]] Empty e;
	int x;
};

static_assert(sizeof(A) == sizeof(int));

int main() {
	A value{};
	value.x = 42;
	return sizeof(A) == sizeof(int) && value.x == 42 ? 0 : 1;
}
