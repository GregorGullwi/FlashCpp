// Reduced non-std stand-in for MSVC ranges CPO begin/end:
// consteval choose() fills a variable template, operator() is constrained by
// that choice, and auto return of a class-type iterator is selected through
// if constexpr. Sema must deduce the taken-branch return type so codegen does
// not attempt a struct-without-conversion-operator fallback.
template <class T>
constexpr bool is_array_like = false;

template <class T>
concept HasMemberBegin = requires(T value) {
	value.begin();
};

enum class Strategy { None, Array, Member };

struct Choice {
	Strategy strategy;
	bool no_throw;
};

template <class Ty>
consteval Choice choose() noexcept {
	if constexpr (is_array_like<Ty>) {
		return {Strategy::Array, true};
	} else if constexpr (HasMemberBegin<Ty>) {
		return {Strategy::Member, true};
	} else {
		return {Strategy::None, false};
	}
}

template <class Ty>
constexpr Choice choice = choose<Ty>();

struct Iter {
	int* p;
};

struct WideIter {
	long long* p;
};

constexpr bool operator==(Iter a, Iter b) {
	return a.p == b.p;
}

constexpr bool operator==(WideIter a, WideIter b) {
	return a.p == b.p;
}

struct Range {
	int* first;
	int* last;
	Iter begin() { return Iter{first}; }
	Iter end() { return Iter{last}; }
};

struct WideRange {
	long long* first;
	long long* last;
	WideIter begin() { return WideIter{first}; }
	WideIter end() { return WideIter{last}; }
};

struct BeginCpo {
	template <class T>
		requires (choice<T&>.strategy != Strategy::None)
	constexpr auto operator()(T&& value) const noexcept(choice<T&>.no_throw) {
		constexpr Strategy strat = choice<T&>.strategy;
		if constexpr (strat == Strategy::Member) {
			return value.begin();
		} else {
			return value;
		}
	}
};

inline constexpr BeginCpo begin_cpo{};

struct EndCpo {
	template <class T>
		requires (choice<T&>.strategy != Strategy::None)
	constexpr auto operator()(T&& value) const noexcept(choice<T&>.no_throw) {
		constexpr Strategy strat = choice<T&>.strategy;
		if constexpr (strat == Strategy::Member) {
			return value.end();
		} else {
			return value;
		}
	}
};

inline constexpr EndCpo end_cpo{};

int main() {
	int xs[2] = {1, 2};
	Range r{xs, xs + 2};
	if (begin_cpo(r) == end_cpo(r)) {
		return 1;
	}

	long long ys[2] = {8, 9};
	WideRange w{ys, ys + 2};
	WideIter first = begin_cpo(w);
	return first.p == ys && !(begin_cpo(w) == end_cpo(w)) ? 0 : 2;
}
