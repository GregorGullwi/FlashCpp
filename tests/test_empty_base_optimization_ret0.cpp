struct Empty {};

struct D : Empty {
	int x;
};

static_assert(sizeof(D) == sizeof(int));

int main() {
	D value{};
	value.x = 42;
	return sizeof(D) == sizeof(int) && value.x == 42 ? 0 : 1;
}
