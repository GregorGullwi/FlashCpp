// C++20 [over.ics.rank]/3.3: two user-defined conversion sequences that share
// the converting constructor are compared by their second standard conversion
// sequences. The by-value sequence is not a reference binding, so the
// less-cv-qualified-reference tie-break does not order it against const Target&.
// Both second sequences are identity conversions, so the call is ambiguous.
struct Source {
	int value;
};

struct Target {
	Target(Source source) : value(source.value) {}
	int value;
};

int choose(Target target) {
	return target.value;
}

int choose(const Target& target) {
	return target.value + 100;
}

int main() {
	Source source{42};
	return choose(source);
}
