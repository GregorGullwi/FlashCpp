// Regression: class-scope noexcept(...) probes must accept identifier calls to
// callable objects whose visible operator() is only a constrained member template.

namespace std_like {
template <class T>
T&& declval() noexcept;
}

struct RangeAccessCpo {
private:
	enum class State { None, Ok };

	struct Choice {
		State _Strategy;
		bool _No_throw;
	};

	template <class T>
	static consteval Choice choose() noexcept {
		return {State::Ok, true};
	}

	template <class T>
	static constexpr Choice choice = choose<T>();

public:
	template <class T>
		requires (choice<T&>._Strategy != State::None)
	constexpr auto operator()(T&&) const noexcept(choice<T&>._No_throw) {
		return 0;
	}
};

inline constexpr RangeAccessCpo begin_like{};
inline constexpr RangeAccessCpo end_like{};

struct DistanceLike {
private:
	template <class It, class Se>
	static constexpr int distance_unchecked(It, Se) noexcept {
		return 0;
	}

public:
	template <class R>
	static constexpr bool nothrow_size = noexcept(
		distance_unchecked(
			begin_like(::std_like::declval<R&>()),
			end_like(::std_like::declval<R&>())));
};

int main() {
	(void) DistanceLike::nothrow_size<int>;
	return 0;
}
