struct S {
	int x;
};

static_assert(sizeof(S{0}) == sizeof(S));

int main() {
	return sizeof(S{0}) == sizeof(S) ? 0 : 1;
}
