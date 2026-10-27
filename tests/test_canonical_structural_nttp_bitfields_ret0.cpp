enum class Code : signed char { negative = -3, other = 2 };
struct Inner { unsigned low : 3; int signed_value : 3; bool flag : 1; Code code : 3; };
struct Key : Inner { unsigned upper : 5; unsigned : 2; unsigned : 0; unsigned tail : 4; };
constexpr Key key{{9u, 7, true, Code::negative}, 33u, 17u};
template<Key Value> struct Base { short member; };
template<class A, class B> struct Same { static constexpr bool value = false; };
template<class A> struct Same<A, A> { static constexpr bool value = true; };
static_assert(Same<Base<key>, Base<Key{{9u, 7, true, Code::negative}, 33u, 17u}>>::value);
static_assert(Same<Base<key>, Base<Key{{1u, -1, true, Code::negative}, 1u, 1u}>>::value);
static_assert(!Same<Base<key>, Base<Key{{2u, 7, true, Code::negative}, 33u, 17u}>>::value);
static_assert(!Same<Base<key>, Base<Key{{9u, -2, true, Code::negative}, 33u, 17u}>>::value);
static_assert(!Same<Base<key>, Base<Key{{9u, 7, false, Code::negative}, 33u, 17u}>>::value);
static_assert(!Same<Base<key>, Base<Key{{9u, 7, true, Code::other}, 33u, 17u}>>::value);
static_assert(!Same<Base<key>, Base<Key{{9u, 7, true, Code::negative}, 33u, 18u}>>::value);
struct Padding { long long guard; };
struct Derived : Padding, Base<key> {};
int main() {
	Derived object;
	object.guard = 12345678901LL;
	object.member = 42;
	Base<key>* base = &object;
	return base->member == 42 && object.guard == 12345678901LL ? 0 : 1;
}
