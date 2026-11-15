// Shape-only queries on pointer and reference operands must keep their
// definite answers even when the underlying record has a deferred base.

struct Base {};

template<class T> struct Derived : T {
	static_assert(__is_trivially_copyable(Derived*));
	static_assert(__is_trivially_destructible(Derived*));
	static_assert(!__is_trivially_copyable(Derived&));
};

int main() {
	Derived<Base> value;
	return 0;
}
