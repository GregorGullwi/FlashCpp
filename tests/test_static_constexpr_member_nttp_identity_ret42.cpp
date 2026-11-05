// A class-type static constexpr data member keeps its typed value identity
// when used as a template argument to another class template.
struct Key {
	int value;
};

struct Holder {
	static constexpr Key key{5};
};

template<Key Value>
struct Base {
	static constexpr int value = Value.value;
};

template<class A, class B>
struct Same {
	static constexpr bool value = false;
};

template<class A>
struct Same<A, A> {
	static constexpr bool value = true;
};

static_assert(Same<Base<Holder::key>, Base<Key{5}>>::value);

int main() {
	return Base<Holder::key>::value == 5 ? 42 : 1;
}
