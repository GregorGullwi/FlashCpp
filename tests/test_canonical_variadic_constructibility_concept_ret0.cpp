// Argument-bearing __is_constructible queries must resolve through constructor
// overload resolution in the lazy constraint evaluator, matching the folded
// path, instead of being left as an unclassified Unknown.
struct TakesInt {
	TakesInt(int) {}
};

struct NoArgs {
};

struct FromRecord {
	FromRecord(NoArgs) {}
};

struct TakesTwo {
	TakesTwo(int, long) {}
};

template <typename Type>
concept ConstructibleFromInt = __is_constructible(Type, int);

template <typename Type>
requires ConstructibleFromInt<Type>
int probe(Type*) {
	return 1;
}

int probe(...) {
	return 2;
}

static_assert(__is_constructible(TakesInt, int), "int constructor");
static_assert(!__is_constructible(FromRecord, int), "record argument is not an int");
static_assert(!__is_constructible(NoArgs, int), "no int constructor");
static_assert(__is_constructible(TakesTwo, int, long), "two-argument constructor");
static_assert(!__is_constructible(TakesTwo, int), "arity mismatch");
static_assert(__is_constructible(NoArgs), "zero-argument constructor");

int main() {
	TakesInt* takes_int = nullptr;
	FromRecord* from_record = nullptr;
	NoArgs* no_args = nullptr;

	int mismatches = 0;
	if (probe(takes_int) != 1) {
		mismatches |= 1;
	}
	if (probe(from_record) != 2) {
		mismatches |= 2;
	}
	if (probe(no_args) != 2) {
		mismatches |= 4;
	}
	return mismatches == 0 ? 0 : 1;
}
