struct Plain {
	int x;
};

struct NonTrivial {
	int x;
	NonTrivial(const NonTrivial&) {}
};

int main() {
	if (!__is_trivially_copyable(Plain)) return 1;
	if (!__is_trivial(Plain)) return 2;
	if (__is_trivially_copyable(NonTrivial)) return 3;
	if (__is_trivial(NonTrivial)) return 4;
	return 42;
}
