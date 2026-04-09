struct Empty {};

struct X : Empty {
	Empty e;
};

static_assert(sizeof(X) >= 2);

int main() {
	return sizeof(X) >= 2 ? 0 : 1;
}
