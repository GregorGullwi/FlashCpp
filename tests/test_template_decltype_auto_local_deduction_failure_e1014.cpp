// A decltype(auto) local in an instantiated template whose initializer type
// cannot be inferred must report AutoTypeDeductionFailure, the same owner the
// auto deduction path already uses, rather than an unlocated string.

struct Empty {};

template <typename T>
void deduce() {
	decltype(auto) value = T::nonexistent;
	(void)value;
}

int main() {
	deduce<Empty>();
	return 0;
}
