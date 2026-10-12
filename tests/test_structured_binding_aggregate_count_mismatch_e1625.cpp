// Aggregate decomposition requires exactly one identifier per non-static data
// member. A mismatch must be a source rejection with StructuredBindingMemberCountMismatch
// rather than an internal codegen failure over a missing binding symbol.

struct Pair {
	int first;
	int second;
};

int main() {
	auto [a, b, c] = Pair{1, 2};
	return a;
}
