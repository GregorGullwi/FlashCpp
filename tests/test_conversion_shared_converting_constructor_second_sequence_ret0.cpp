// C++20 [over.ics.rank]/3.3: two user-defined conversion sequences are ordered
// by their second standard conversion sequence when they contain the same
// conversion function or constructor. Both overloads reach their parameter
// through Target's converting constructor, so their second standard conversion
// sequences are comparable: binding the constructed prvalue to Target&& is
// better than binding it to const Target& ([over.ics.rank]/3.2.3). The call
// must therefore select the Target&& overload for a named lvalue and for a
// prvalue argument instead of staying ambiguous.
struct Source {
	int value;
};

struct Target {
	Target(Source source) : value(source.value) {}
	int value;
};

int select(Target&& target) {
	return target.value;
}

int select(const Target& target) {
	return target.value + 100;
}

int main() {
	Source source{42};
	if (select(source) != 42) {
		return 1;
	}
	if (select(Source{42}) != 42) {
		return 2;
	}
	return 0;
}
