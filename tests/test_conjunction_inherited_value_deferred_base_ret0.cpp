// Reduced from MSVC STL patterns found in <type_traits>/<xmemory>:
//   struct uses_allocator : _Has_allocator_type<_Ty, _Alloc>::type {};
//   inline constexpr bool uses_allocator_v = uses_allocator<_Ty, _Alloc>::value;
// and
//   struct conjunction<_First, _Rest...>
//       : _Conjunction<static_cast<bool>(_First::value), _Rest...>::type {};
//
// Instantiating the deferred-base NTTP `static_cast<bool>(First::value)` must
// resolve First::value even when `value` is an INHERITED static member reached
// through the trait's own deferred base chain (`PickBox<N>::type` here).

template <bool Value>
struct BoolBox {
	static constexpr bool value = Value;
};

using TrueBox = BoolBox<true>;
using FalseBox = BoolBox<false>;

template <bool Condition>
struct PickBox {
	using type = TrueBox;
};

template <>
struct PickBox<false> {
	using type = FalseBox;
};

// Traits whose 'value' is inherited through a deferred base with a ::type
// member chain (mirrors MSVC uses_allocator : _Has_allocator_type<...>::type).
template <class Type>
struct HasWide : PickBox<(sizeof(Type) > sizeof(int))>::type {};

template <class Type>
struct IsBig : PickBox<(sizeof(Type) >= sizeof(long long))>::type {};

// MSVC-style conjunction machinery.
template <bool FirstValue, class First, class... Rest>
struct ConjStep {
	using type = First;
};

template <class First, class Next, class... Rest>
struct ConjStep<true, First, Next, Rest...> {
	using type = typename ConjStep<static_cast<bool>(Next::value), Next, Rest...>::type;
};

template <class First>
struct ConjStep<true, First> {
	using type = TrueBox;
};

template <class... Traits>
struct MyConjunction : TrueBox {};

template <class First, class... Rest>
struct MyConjunction<First, Rest...>
	: ConjStep<static_cast<bool>(First::value), First, Rest...>::type {};

template <class... Traits>
constexpr bool MyConjunctionV = MyConjunction<Traits...>::value;

template <class Type>
void maybeSwap(Type& left, Type& right) noexcept(MyConjunctionV<HasWide<Type>>) {
	Type tmp = left;
	left = right;
	right = tmp;
}

struct Tiny {
	char byte;
};

struct Wide {
	int v0;
	int v1;
	int v2;
	int v3;
};

int main() {
	Tiny tiny_a{'A'};
	Tiny tiny_b{'B'};
	Wide wide_a{1, 2, 3, 4};
	Wide wide_b{5, 6, 7, 8};
	maybeSwap(tiny_a, tiny_b);
	maybeSwap(wide_a, wide_b);
	const bool tiny_nothrow =
		noexcept(maybeSwap(tiny_a, tiny_b));
	const bool wide_nothrow =
		noexcept(maybeSwap(wide_a, wide_b));
	if (tiny_nothrow) return 50;
	if (!wide_nothrow) return 51;
	if (!MyConjunctionV<IsBig<Wide>>) return 52;
	if (!MyConjunctionV<>) return 53;
	if (!MyConjunctionV<HasWide<Wide>>) return 54;
	// Side effects of the reference-parameter swaps above: implicit
	// copy-assignment through references must copy every member.
	if (tiny_a.byte != 'B') return 55;
	if (tiny_b.byte != 'A') return 56;
	if (wide_a.v0 != 5) return 57;
	if (wide_b.v0 != 1) return 58;
	return 0;
}
