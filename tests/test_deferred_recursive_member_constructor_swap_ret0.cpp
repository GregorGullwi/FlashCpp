// Regression: a recursively inherited variadic class must replay an
// out-of-line forwarding constructor with the concrete base specialization
// available, while an unqualified swap probe is being instantiated.

template<class T>
T&& probe_declval() noexcept;

template<class T>
void swap(T& left, T& right) noexcept {
	T temporary = left;
	left = right;
	right = temporary;
}

template<class T>
struct Value {
	T value;

	Value() : value() {}

	template<class U>
	Value(U&& input) : value(static_cast<U&&>(input)) {}
};

template<class... Types>
struct Recursive;

template<>
struct Recursive<> {
	Recursive() {}
	void swap(Recursive&) {}
};

template<class Head, class... Tail>
struct Recursive<Head, Tail...> : Recursive<Tail...> {
	using Base = Recursive<Tail...>;
	Value<Head> first;

	Recursive() : Base(), first() {}

	template<class U, class... Us>
	Recursive(U&& head, Us&&... tail);

	Base& rest() noexcept { return *this; }
	const Base& rest() const noexcept { return *this; }

	void swap(Recursive& other) {
		::swap(first.value, other.first.value);
		Base::swap(other.rest());
	}
};

template<class Head, class... Tail>
template<class U, class... Us>
Recursive<Head, Tail...>::Recursive(U&& head, Us&&... tail)
	: Base(static_cast<Us&&>(tail)...), first(static_cast<U&&>(head)) {}

template<class T, class = decltype(swap(probe_declval<T&>(), probe_declval<T&>()))>
struct IsSwappable {
	static constexpr bool value = true;
};

int main() {
	static_assert(IsSwappable<Recursive<int, float, double>>::value);
	Recursive<int, float, double> left(1, 2.0f, 3.0);
	Recursive<int, float, double> right(4, 5.0f, 6.0);
	left.swap(right);
	return left.first.value == 4 ? 0 : 1;
}
