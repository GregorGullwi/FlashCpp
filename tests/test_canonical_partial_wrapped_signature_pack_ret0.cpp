template <class... T>
struct PackTypes {};

template <class T, class U>
struct SamePackTypes {
	static constexpr bool value = false;
};

template <class T>
struct SamePackTypes<T, T> {
	static constexpr bool value = true;
};

template <class Signature>
struct WrappedSignature {
	static constexpr int value = 0;
	using types = PackTypes<>;
};

template <class R, class... T>
struct WrappedSignature<R (*)(T*...)> {
	static constexpr int value = 1;
	using types = PackTypes<T...>;
};

struct Record {};

using Mixed = WrappedSignature<int (*)(char*, const long*, Record**)>;
using Empty = WrappedSignature<short (*)()>;
using LastMismatch = WrappedSignature<int (*)(char*, long)>;
using FirstMismatch = WrappedSignature<int (*)(char, long*)>;

static_assert(Mixed::value == 1);
static_assert(SamePackTypes<Mixed::types, PackTypes<char, const long, Record*>>::value);
static_assert(Empty::value == 1);
static_assert(SamePackTypes<Empty::types, PackTypes<>>::value);
static_assert(LastMismatch::value == 0);
static_assert(FirstMismatch::value == 0);

template <class First, class Second>
struct RepeatedPack {
	static constexpr int value = 0;
};

template <class... T>
struct RepeatedPack<int (*)(short, T*...), void (*)(T&...)> {
	static constexpr int value = 1;
};

using SameElements = RepeatedPack<int (*)(short, char*, long*), void (*)(char&, long&)>;
using DifferentTail = RepeatedPack<int (*)(short, char*, long*), void (*)(char&, int&)>;
using DifferentLength = RepeatedPack<int (*)(short, char*, long*), void (*)(char&)>;
using IncorrectPrefix = RepeatedPack<int (*)(long, char*, long*), void (*)(char&, long&)>;

static_assert(SameElements::value == 1);
static_assert(DifferentTail::value == 0);
static_assert(DifferentLength::value == 0);
static_assert(IncorrectPrefix::value == 0);

template <class Signature>
struct CVariadicSignature {
	static constexpr int value = 0;
};

// With a scalar template parameter, the ellipsis is the C variadic suffix.
template <class R, class T>
struct CVariadicSignature<R (*)(T*...)> {
	static constexpr int value = 1;
};

using CVariadic = CVariadicSignature<int (*)(char*...)>;
static_assert(CVariadic::value == 1);

int main() {
	return Mixed::value + Empty::value + LastMismatch::value + FirstMismatch::value +
		SameElements::value + DifferentTail::value + DifferentLength::value + IncorrectPrefix::value + CVariadic::value - 4;
}
