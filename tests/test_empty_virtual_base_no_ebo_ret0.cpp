struct Empty {};

struct V : virtual Empty {
	int x;
};

static_assert(sizeof(V) > sizeof(int));

int main() {
	return sizeof(V) > sizeof(int) ? 0 : 1;
}
