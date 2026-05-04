// Test: pack expansion with ::type member access in base class list
// Pattern: identity<Args>::type... where ... follows ::type not >
// Regression test for bug where ... after ::type was not consumed
template <typename T>
struct identity {
	using type = T;
};

struct A {};
struct B {};

int readTag(const A&) {
	return 19;
}

int readTag(const B&) {
	return 23;
}

template <typename... Args>
struct Multi : identity<Args>::type... {
};

int main() {
	Multi<A, B> m;
	return readTag(static_cast<A&>(m)) + readTag(static_cast<B&>(m)) - 42;
}
