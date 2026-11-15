// Same deferred-base rule for emptiness: the pattern-time answer used to be a
// definitive "not empty", accepting the negated assertion before the base was
// known and skipping the instantiation re-evaluation.

struct EmptyBase {};

template<class T> struct Derived : T {
	static_assert(!__is_empty(Derived), "must not be empty");
};

int main() {
	Derived<EmptyBase> value;
	return 0;
}
