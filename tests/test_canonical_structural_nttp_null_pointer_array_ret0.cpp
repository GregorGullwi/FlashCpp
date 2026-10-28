struct Key {
	int* pointers[2];
	int tag;
};

constexpr Key first{{nullptr, nullptr}, 1};
constexpr Key second{{nullptr, nullptr}, 2};

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

struct Padding {
	long long guard;
};

struct Derived : Padding, Base<first> {};

static_assert(Same<Base<first>, Base<Key{{nullptr, nullptr}, 1}>>::value);
static_assert(!Same<Base<first>, Base<second>>::value);

int main() {
	Derived object;
	object.guard = 12345678901LL;
	object.member = 42;
	Base<first>* base = &object;
	return base->member == 42 && object.guard == 12345678901LL ? 0 : 1;
}
