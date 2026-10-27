enum class Code : signed char { negative = -3, other = 2 };
struct Element { short value; };
struct Key { int values[3]; Element elements[2]; int matrix[2][2]; bool flags[2]; Code codes[2]; };
constexpr Key key{{1u, 2u, 3u}, {{4}, {5}}, {{6, 7}, {8, 9}}, {true, false}, {Code::negative, Code::other}};
template<Key Value> struct Base { short member; };
template<class A, class B> struct Same { static constexpr bool value = false; };
template<class A> struct Same<A, A> { static constexpr bool value = true; };
static_assert(Same<Base<key>, Base<Key{{1u, 2u, 3u}, {{4}, {5}}, {{6, 7}, {8, 9}}, {true, false}, {Code::negative, Code::other}}>>::value);
static_assert(!Same<Base<key>, Base<Key{{1u, 2u, 4u}, {{4}, {5}}, {{6, 7}, {8, 9}}, {true, false}, {Code::negative, Code::other}}>>::value);
static_assert(!Same<Base<key>, Base<Key{{1u, 2u, 3u}, {{4}, {6}}, {{6, 7}, {8, 9}}, {true, false}, {Code::negative, Code::other}}>>::value);
static_assert(!Same<Base<key>, Base<Key{{1u, 2u, 3u}, {{4}, {5}}, {{6, 7}, {8, 10}}, {true, false}, {Code::negative, Code::other}}>>::value);
static_assert(!Same<Base<key>, Base<Key{{1u, 2u, 3u}, {{4}, {5}}, {{6, 7}, {8, 9}}, {false, true}, {Code::negative, Code::other}}>>::value);
static_assert(!Same<Base<key>, Base<Key{{1u, 2u, 3u}, {{4}, {5}}, {{6, 7}, {8, 9}}, {true, false}, {Code::other, Code::negative}}>>::value);
struct Padded { int values[3]; };
constexpr Padded padded{{1}};
template<Padded Value> struct PaddedBase { short member; };
static_assert(Same<PaddedBase<padded>, PaddedBase<Padded{{1, 0, 0}}>>::value);
struct Padding { long long guard; };
struct Derived : Padding, Base<key> {};
int main() {
	Derived object;
	object.guard = 12345678901LL;
	object.member = 42;
	Base<key>* base = &object;
	return base->member == 42 && object.guard == 12345678901LL ? 0 : 1;
}
