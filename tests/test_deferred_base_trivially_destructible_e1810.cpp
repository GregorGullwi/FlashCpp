// A record trait evaluated over a deferred base must stay unknown so the
// static_assert is re-evaluated at instantiation instead of being accepted at
// pattern time. Here the deferred walk previously answered "not trivially
// destructible", which accepted the negated assertion for a trivial base.

struct TrivialBase {};

template<class T> struct Derived : T {
	static_assert(!__is_trivially_destructible(Derived), "must not be trivially destructible");
};

int main() {
	Derived<TrivialBase> value;
	return 0;
}
