// Reference-based: const int& -> double& changes the underlying type,
// which is not a valid const_cast (only cv/ref changes are allowed).
constexpr int value = 42;
constexpr const int& const_ref = value;

constexpr double bad_const_cast_ref() {
	return const_cast<double&>(const_ref);
}

static_assert(bad_const_cast_ref() == 0);

int main() {
	return 0;
}
