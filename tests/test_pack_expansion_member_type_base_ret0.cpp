// Test: pack expansion with ::type member access in base class list
// Pattern: identity<Args>::type... where ... follows ::type not >
// Regression test for bug where ... after ::type was not consumed
template<typename T>
struct identity {
	using type = T;
};

struct A { int a; };
struct B { int b; };

template<typename... Args>
struct Multi : identity<Args>::type... {
};

int main() {
	Multi<A, B> m;
	(void)m;
	return 0;
}
