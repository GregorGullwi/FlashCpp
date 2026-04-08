template<typename T>
struct pick;

template<typename X>
struct pick<X*> {
	static constexpr int value = X::v;
};

struct A {
	static constexpr int v = 1;
};

struct B {
	static constexpr int v = 2;
};

template<typename X>
constexpr int g() {
	return pick<B*>::value;
}

template<typename X>
constexpr int h() {
	return pick<X*>::value;
}

int main() {
	static_assert(g<A>() == 2);
	static_assert(h<B>() == 2);
	return g<A>() == 2 && h<B>() == 2 ? 0 : 1;
}
