struct Empty {};

struct S {
	[[no_unique_address]] Empty first;
	[[no_unique_address]] Empty second;
	char c;
};

static_assert(sizeof(S) == 2);
static_assert(offsetof(S, c) == 0);

int main() {
	S value{};
	value.c = 42;
	return sizeof(S) == 2 && offsetof(S, c) == 0 && value.c == 42 ? 0 : 1;
}
