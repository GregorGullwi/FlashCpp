// A pointer to a deferred-base record is still trivially copyable; the guard
// must not make the shape-only answer unknown while the base is unresolved.

struct Base {};

template<class T> struct Derived : T {
	static_assert(!__is_trivially_copyable(Derived*), "pointer is trivially copyable");
};

int main() {
	Derived<Base> value;
	return 0;
}
