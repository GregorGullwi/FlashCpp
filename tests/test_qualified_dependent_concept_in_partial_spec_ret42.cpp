namespace traits {

template <class Left, class Right>
concept Same = __is_same(Left, Right);

enum class RangeKind : bool {
	unsized,
	sized
};

template <
	class Iterator,
	class Sentinel,
	RangeKind Kind =
		Same<Iterator, Sentinel>
			? RangeKind::sized
			: RangeKind::unsized>
struct Range {
	static constexpr RangeKind kind = Kind;
};

template <class Iterator, class Sentinel, RangeKind Kind>
struct RangeFactory {
	using type = Range<Iterator, Sentinel, Kind>;
};

template <class Iterator, class Sentinel>
struct RangeFactory<Iterator, Sentinel, RangeKind::sized> {
	template <class Other>
	static constexpr RangeKind reboundKind() {
		return Range<Other, Other>::kind;
	}
};

}

struct LargeIterator {
	long long value;
};

int main() {
	using Selected = traits::RangeFactory<
			LargeIterator,
			char,
			traits::RangeKind::sized>;
	return Selected::reboundKind<LargeIterator>() ==
			traits::RangeKind::sized
		? 42
		: 0;
}
