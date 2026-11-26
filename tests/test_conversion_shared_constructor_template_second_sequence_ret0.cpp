// C++20 [over.ics.rank]/3.3: two user-defined conversion sequences are ordered by
// their second standard conversion sequence when they contain the same conversion
// function or constructor. Target's only converting constructor is a template, so
// its published canonical schema cannot select it and both candidates reach the
// constructor through the declaration bridge. The bridge still names the same
// schema entry, so both sequences share one constructor identity and binding the
// constructed prvalue to Target&& beats binding it to const Target&
// ([over.ics.rank]/3.2.3) for a named lvalue and for a prvalue argument. Before
// the bridge mapped its selection to the canonical schema index both sequences
// kept no identity and the call stayed ambiguous.
struct Source {
	int value;
};

struct Target {
	template <class T>
	Target(T source) : value(source.value) {}
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
	if (select(Source{42}) != 42) {
		return 1;
	}
	if (select(source) != 42) {
		return 2;
	}
	return 0;
}
