// Reduced from libstdc++ <type_traits> __make_unsigned_selector / make_unsigned:
// a class template with a required type parameter and later NTTP defaults that
// name prior parameters via Trait<Tp>::value, used as Selector<Tp>::type
// (one explicit argument). Mix native types and a user-defined struct.

template <typename>
struct IsIntegral {
	static constexpr bool value = false;
};

template <>
struct IsIntegral<int> {
	static constexpr bool value = true;
};

template <>
struct IsIntegral<unsigned int> {
	static constexpr bool value = true;
};

template <>
struct IsIntegral<char> {
	static constexpr bool value = true;
};

template <typename>
struct IsConst {
	static constexpr bool value = false;
};

template <typename Tp>
struct IsConst<const Tp> {
	static constexpr bool value = true;
};

template <typename>
struct IsVolatile {
	static constexpr bool value = false;
};

template <typename Tp>
struct IsVolatile<volatile Tp> {
	static constexpr bool value = true;
};

template <typename Qualified, typename Unqualified,
		  bool = IsConst<Qualified>::value,
		  bool = IsVolatile<Qualified>::value>
struct MatchCv {
	using type = Unqualified;
};

template <typename Tp>
struct RemoveCv {
	using type = Tp;
};

template <typename Tp>
struct RemoveCv<const Tp> {
	using type = Tp;
};

template <typename Tp>
struct UnsignedMap;

template <>
struct UnsignedMap<int> {
	using type = unsigned int;
};

template <>
struct UnsignedMap<unsigned int> {
	using type = unsigned int;
};

template <>
struct UnsignedMap<char> {
	using type = unsigned char;
};

template <typename Tp, bool = IsIntegral<Tp>::value, bool = false>
class Selector;

template <typename Tp>
class Selector<Tp, true, false> {
	using UnsignedType = typename UnsignedMap<typename RemoveCv<Tp>::type>::type;

public:
	using type = typename MatchCv<Tp, UnsignedType>::type;
};

template <typename Tp>
struct MakeUnsigned {
	using type = typename Selector<Tp>::type;
};

struct Probe {
	char field;
};

int main() {
	using FromInt = MakeUnsigned<int>::type;
	using FromConstInt = MakeUnsigned<const int>::type;
	using FromChar = MakeUnsigned<char>::type;
	using ExplicitInt = Selector<int, true, false>::type;
	using ExplicitConstInt = Selector<const int, true, false>::type;
	(void)sizeof(Probe);
	return (sizeof(FromInt) == sizeof(unsigned int) &&
			sizeof(FromConstInt) == sizeof(unsigned int) &&
			sizeof(FromChar) == sizeof(unsigned char) &&
			sizeof(ExplicitInt) == sizeof(unsigned int) &&
			sizeof(ExplicitConstInt) == sizeof(unsigned int))
			   ? 0
			   : 1;
}
