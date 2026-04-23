// Test: implicit deduction of pack element types from Box<Ts>... function parameters.
//
// When the function-parameter pack has a template-specialisation element type
// (e.g. Box<Ts>...), implicit deduction must extract the inner type Ts from each
// call argument (Box<int> -> int, Box<double> -> double) rather than deducing
// the outer type directly (Box<int>, Box<double>).
//
// Prior to the Phase 6 fix, buildDeductionMapFromCallArgs skipped the pack slot
// entirely; deduceTemplateArgsFromCall then pushed the raw call-arg types, giving
// Ts = {Box<int>, Box<double>} instead of Ts = {int, double}.  That incorrect
// deduction caused the instantiation key to mismatch the actual argument types,
// resulting in either a compile error or a wrong runtime value.

template<typename T>
struct Box {
	T value;
	Box(T v) : value(v) {}
};

// Helper: sum a heterogeneous pack via fold expression
template<typename... Ts>
int sum_cast(Ts... vals) {
	return (0 + ... + static_cast<int>(vals));
}

// Primary test: implicit Ts deduction from Box<Ts>... pack
// With correct deduction Ts={int,double}: boxes.value... = {3, 7} -> sum = 10
// With wrong deduction Ts={Box<int>,Box<double>}: would fail to compile
template<typename... Ts>
int sum_boxes(Box<Ts>... boxes) {
	return sum_cast(boxes.value...);
}

// Secondary test: mixed non-pack + pack with template-specialisation pack
// T is deduced from the first (non-pack) argument; Ts from the rest.
template<typename T, typename... Ts>
int first_plus_sum(T first, Box<Ts>... rest) {
	return static_cast<int>(first) + sum_cast(rest.value...);
}

int main() {
	Box<int>    a(3);
	Box<double> b(7.0);
	Box<int>    c(2);

	// sum_boxes: Ts should be deduced as {int, double}
	// result = 3 + 7 = 10
	int r1 = sum_boxes(a, b);
	if (r1 != 10) return 1;

	// sum_boxes with three arguments: Ts = {int, double, int}
	// result = 3 + 7 + 2 = 12
	int r2 = sum_boxes(a, b, c);
	if (r2 != 12) return 2;

	// first_plus_sum: T = int (from a.value), Ts = {double} (from b)
	// result = 3 + 7 = 10
	int r3 = first_plus_sum(a.value, b);
	if (r3 != 10) return 3;

	return 0;
}
