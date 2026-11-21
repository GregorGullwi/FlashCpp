// C++20 compares a structural non-type template argument member by member, so a
// function pointer member contributes the function it points to. Two values that
// differ only in that target must instantiate different specializations.
void first_target() {}
void second_target() {}

struct Key {
	void (*fn)();
	int tag;
};

constexpr Key key_first{&first_target, 1};
constexpr Key key_second{&second_target, 1};
constexpr Key key_retagged{&first_target, 2};

template<class A, class B> struct Same { static constexpr bool value = false; };
template<class A> struct Same<A, A> { static constexpr bool value = true; };

template<Key Value> struct Holder { int marker; };

int main() {
	int result = 0;
	if (!Same<Holder<key_first>, Holder<key_first>>::value) {
		result |= 1;
	}
	if (Same<Holder<key_first>, Holder<key_second>>::value) {
		result |= 2;
	}
	if (Same<Holder<key_first>, Holder<key_retagged>>::value) {
		result |= 4;
	}
	return result;
}
