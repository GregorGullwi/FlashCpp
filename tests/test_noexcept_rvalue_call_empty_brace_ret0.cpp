// Regression: noexcept on rvalue call operator with move/forward and Type{} arg.
// MSVC __msvc_ranges_to.hpp pattern.

template <class T>
constexpr T&& move(T& value) noexcept {
	return static_cast<T&&>(value);
}

template <class T>
constexpr T&& forward(T&& value) noexcept {
	return static_cast<T&&>(value);
}

template <class... Types>
struct Closure {
	template <class Ty>
	static constexpr bool call(Closure&& self, Ty&& arg, int) {
		(void)self;
		(void)arg;
		return true;
	}

	using Indices = int;

	template <class Ty>
	constexpr bool operator()(Ty&& arg) && noexcept(
		noexcept(call(move(*this), forward<Ty>(arg), Indices{}))) {
		return call(move(*this), forward<Ty>(arg), Indices{});
	}
};

int main() {
	return Closure<>{}(1) ? 0 : 1;
}
