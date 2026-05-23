// Regression test: constexpr-folded ternary with wchar_t/int branches must use
// the sema-computed common type (wchar_t), not just the evaluated branch type (int).
// This mirrors the numeric_limits<wchar_t>::min()/max() pattern from <limits>.
struct wchar_limits {
	static constexpr wchar_t
	min() noexcept {
		return (((wchar_t)(-1) < 0) ?
			-(((wchar_t)(-1) < 0) ?
				(((((wchar_t)1 << ((sizeof(wchar_t) * 8 - ((wchar_t)(-1) < 0)) - 1)) - 1) << 1) + 1) :
				~(wchar_t)0) - 1 :
			(wchar_t)0);
	}
	static constexpr wchar_t
	max() noexcept {
		return (((wchar_t)(-1) < 0) ?
			(((((wchar_t)1 << ((sizeof(wchar_t) * 8 - ((wchar_t)(-1) < 0)) - 1)) - 1) << 1) + 1) :
			~(wchar_t)0);
	}
};

int main() {
	constexpr wchar_t lo = wchar_limits::min();
	constexpr wchar_t hi = wchar_limits::max();
	return (lo < hi) ? 0 : 1;
}
