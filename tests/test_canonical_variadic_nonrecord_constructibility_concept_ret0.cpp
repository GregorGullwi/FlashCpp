// Argument-bearing constructibility for a scalar or pointer target uses the
// implicit conversion rules and must be answered in the lazy constraint
// evaluator, matching the folded path.
template <typename Type>
concept ConstructibleFromDouble = __is_constructible(Type, double);

template <typename Type>
requires ConstructibleFromDouble<Type>
int probe(Type*) {
	return 1;
}

int probe(...) {
	return 2;
}

static_assert(__is_constructible(int, double), "int from double");
static_assert(__is_constructible(long, double), "long from double");
static_assert(!__is_constructible(int*, double), "pointer from double");
static_assert(__is_constructible(int*, int*), "pointer from pointer");
static_assert(!__is_constructible(int*, int), "pointer from int");

int main() {
	int scalar = 0;
	int* pointer = &scalar;
	int** pointer_pointer = &pointer;

	int mismatches = 0;
	if (probe(&scalar) != 1) {
		mismatches |= 1;
	}
	if (probe(pointer_pointer) != 2) {
		mismatches |= 2;
	}
	return mismatches == 0 ? 0 : 1;
}
