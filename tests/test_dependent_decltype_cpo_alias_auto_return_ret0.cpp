template <class T>
T&& declval();

namespace api {
	struct SizeFunction {
		template <class Range>
		constexpr unsigned long long operator()(Range& range) const;
	};

	inline constexpr SizeFunction size;

	template <class Range>
	using range_size_t = decltype(size(declval<Range&>()));

	template <class Range>
	struct DropView {
		Range range;
		range_size_t<Range> count;

		constexpr auto size() {
			const auto current = range.size();
			if (current < count) {
				return range_size_t<Range>{0};
			} else {
				return static_cast<range_size_t<Range>>(current - count);
			}
		}
	};
}

struct SmallRange {
	unsigned long long count;

	constexpr unsigned long long size() {
		return count;
	}
};

int main() {
	api::DropView<SmallRange> view{{7}, 2};
	using Result = decltype(view.size());
	return sizeof(Result) == sizeof(unsigned long long) && view.size() == 5 ? 0 : 1;
}
