// Regression reduced from a standard-header constexpr helper: sema must keep
// the exact alias-backed result type of a conditional expression.

namespace meta {
	using Size = unsigned long long;

	inline constexpr Size not_found = static_cast<Size>(-1);

	template <class T>
	struct Result {
		T value;
	};

	constexpr Size findNext(const bool* values, Size count, Size first) {
		for (Size index = first; index < count; ++index) {
			if (values[index]) {
				return index;
			}
		}
		return not_found;
	}

	namespace detail {
		constexpr Size findUnique(const bool* values, Size count, Size first) {
			return first != not_found && meta::findNext(values, count, first + 1) == not_found
				? first
				: not_found;
		}
	}
}

int main() {
	bool values[] = {false, true, false};
	meta::Result<meta::Size> result{meta::detail::findUnique(values, 3, 1)};
	return result.value == 1 ? 0 : 1;
}
