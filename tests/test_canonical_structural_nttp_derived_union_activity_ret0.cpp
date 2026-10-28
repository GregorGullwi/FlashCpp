struct Root { int root; };
struct Key : Root { union { int first; long long second; }; };

constexpr Key make_second() {
	Key value{{7}, {1}};
	value.second = 2;
	return value;
}

constexpr Key first{{7}, {1}};
constexpr Key second = make_second();

template<class A, class B>
struct Same {
	static constexpr bool value = false;
};

template<class A>
struct Same<A, A> {
	static constexpr bool value = true;
};

template<Key Value>
struct Base {
	short member;
};

static_assert(Same<Base<first>, Base<Key{{7}, {1}}>>::value);
static_assert(!Same<Base<first>, Base<second>>::value);

struct Padding {
	long long guard;
};

struct Derived : Padding, Base<first> {};

int main() { return 0; }
