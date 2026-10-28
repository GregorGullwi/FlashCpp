template<class T> struct UnionBits { union { T first : 3; T second : 3; }; unsigned char tag; };
template<class T> struct Bits { T value : 3; unsigned char tag; };
template<class T> struct Key { union { T first; T second; }; short tag; };
template<class T> union Choice { T first; T second; };
template<class T> struct Outer { struct Nested { union { T first; T second; }; unsigned char tag; }; };

constexpr UnionBits<short> union_bits{.second = -1, .tag = 7};
constexpr Bits<short> bits{-1, 7};
constexpr Key<short> first{.first = 0, .tag = 1};
constexpr Key<short> second{.second = 0, .tag = 1};
constexpr Key<long long> wide{.second = 0, .tag = 1};
constexpr Choice<short> choice{.second = 0};

template<UnionBits<short> Value> struct UnionBitTag {};
template<Bits<short> Value> struct BitTag {};
template<Key<short> Value> struct Tag {};
template<Key<long long> Value> struct WideTag {};
template<Choice<short> Value> struct ChoiceTag {};
using UnionBitNamed = UnionBitTag<union_bits>;
using UnionBitLiteral = UnionBitTag<UnionBits<short>{.second = -1, .tag = 7}>;
using BitNamed = BitTag<bits>;
using BitLiteral = BitTag<Bits<short>{-1, 7}>;
using Named = Tag<second>;
using Literal = Tag<Key<short>{.second = 0, .tag = 1}>;
using Other = Tag<first>;
using Wide = WideTag<wide>;
using UnionNamed = ChoiceTag<choice>;
using UnionLiteral = ChoiceTag<Choice<short>{.second = 0}>;

template<class A, class B> struct Same { static constexpr bool value = false; };
template<class A> struct Same<A, A> { static constexpr bool value = true; };
static_assert(Same<UnionBitNamed, UnionBitLiteral>::value);
static_assert(Same<BitNamed, BitLiteral>::value);
static_assert(Same<Named, Literal>::value);
static_assert(!Same<Named, Other>::value);
static_assert(Same<UnionNamed, UnionLiteral>::value);
static_assert(sizeof(Key<long long>) == 16);
static_assert(alignof(Key<long long>) == 8);
static_assert(sizeof(Outer<long long>::Nested) == 16);
static_assert(alignof(Outer<long long>::Nested) == 8);

int main() {
	UnionBits<unsigned short> bit_object{.second = 7, .tag = 7};
	Key<short> object{.second = 42, .tag = 7};
	Outer<long long>::Nested nested{.second = 44, .tag = 9};
	return bit_object.second == 7 && bit_object.tag == 7 && object.second == 42 && object.tag == 7 && nested.second == 44 && nested.tag == 9 ? 0 : 1;
}
