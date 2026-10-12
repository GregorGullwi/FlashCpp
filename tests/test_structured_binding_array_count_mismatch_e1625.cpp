// Array decomposition requires exactly one identifier per element, so a bound
// identifier count that does not match the array extent is ill-formed and must
// report StructuredBindingMemberCountMismatch instead of failing internally.

int main() {
	int values[3] = {1, 2, 3};
	auto [first, second] = values;
	return first;
}
