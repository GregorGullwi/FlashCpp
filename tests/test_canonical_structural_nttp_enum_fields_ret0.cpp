enum Tiny : signed char { negative = -7, different = -6 };
enum class Wide : unsigned long long { high = 0xfedcba9876543210ULL, other = 0xfedcba9876543211ULL };
enum class Code : unsigned short { chosen = 60000 };
enum Plain { plain_negative = -9 };
struct EnumBase { Tiny value; };
struct Nested { Wide wide; Code code; Plain plain; };
struct Key : EnumBase { Nested nested; bool enabled; };
constexpr Key key{{negative}, {Wide::high, Code::chosen, plain_negative}, true};
template<Key Value> struct Base { short member; };
template<class A, class B> struct Same { static constexpr bool value = false; };
template<class A> struct Same<A, A> { static constexpr bool value = true; };
static_assert(Same<Base<key>, Base<Key{{negative}, {Wide::high, Code::chosen, plain_negative}, true}>>::value);
static_assert(!Same<Base<key>, Base<Key{{different}, {Wide::high, Code::chosen, plain_negative}, true}>>::value);
static_assert(!Same<Base<key>, Base<Key{{negative}, {Wide::other, Code::chosen, plain_negative}, true}>>::value);
struct Padding { long long guard; };
struct Derived : Padding, Base<Key{{negative}, {Wide::high, Code::chosen, plain_negative}, true}> {};
int main() {
	Derived object;
	object.guard = 12345678901LL;
	object.member = 42;
	Base<key>* base = &object;
	return base->member == 42 && object.guard == 12345678901LL ? 0 : 1;
}
