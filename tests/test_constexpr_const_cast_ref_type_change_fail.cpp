// Reference-based: const int& -> long& changes the underlying type,
// which is not a valid const_cast (only cv/ref changes are allowed).
constexpr int value = 42;

constexpr long bad_const_cast_ref() {
	return const_cast<long&>(static_cast<const long&>(value));
}

static_assert(bad_const_cast_ref() == 0);

int main() {
	return 0;
}
