// Regression: a deferred concept-id used inside a default NTTP expression
// must collapse to a concrete bool after template substitution.
// Reduced from MSVC STL ranges::subrange's enum-typed default:
//   sized_sentinel_for<_Se, _It> ? subrange_kind::sized : subrange_kind::unsized

template<typename Se, typename It>
concept sized_sentinel_like = __is_same(Se, It);

enum class subrange_kind : bool {
	unsized,
	sized
};

template<typename It, typename Se = It,
	subrange_kind Kind =
		sized_sentinel_like<Se, It> ? subrange_kind::sized : subrange_kind::unsized>
struct subrange_like {
	static constexpr subrange_kind kind = Kind;
};

struct sentinel {};

int main() {
	static_assert(subrange_like<int*>::kind == subrange_kind::sized);
	static_assert(subrange_like<int*, sentinel>::kind == subrange_kind::unsized);
	return 0;
}
