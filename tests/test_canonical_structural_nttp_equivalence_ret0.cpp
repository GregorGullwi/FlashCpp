struct Key { short code; unsigned count; bool enabled; };
constexpr Key key{7, 4000000000u, true};
template<Key Value> struct Base { short member; };
template<class A, class B> struct Same { static constexpr bool value = false; };
template<class A> struct Same<A, A> { static constexpr bool value = true; };
static_assert(Same<Base<key>, Base<Key{7, 4000000000u, true}>>::value);
static_assert(!Same<Base<key>, Base<Key{8, 4000000000u, true}>>::value);
struct Padding { long long guard; };
struct Derived : Padding, Base<Key{7, 4000000000u, true}> {};
int main() {
	Derived object;
	object.guard = 12345678901LL;
	object.member = 42;
	Base<key>* base = &object;
	return base->member == 42 && object.guard == 12345678901LL ? 0 : 1;
}
