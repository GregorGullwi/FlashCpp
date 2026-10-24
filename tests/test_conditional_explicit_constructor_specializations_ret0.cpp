template<class T> struct Enabled { static constexpr bool value = false; };
template<> struct Enabled<short> { static constexpr bool value = true; };
template<class T> struct Box {
	explicit(Enabled<T>::value) Box(T input) : value(input) {}
	T value;
};
template<bool Flag, class T> struct Partial;
template<bool Flag, class T> struct Partial<Flag, T*> {
	explicit(Flag) Partial(T input) : value(input) {}
	T value;
};
struct Selected { char marker[3]; };
struct Rejected { char marker[7]; };
Selected select_short(Box<short>);
Rejected select_short(...);
Selected select_double(Box<double>);
Rejected select_double(...);
static_assert(sizeof(decltype(select_short(short{}))) == sizeof(Rejected));
static_assert(sizeof(decltype(select_double(double{}))) == sizeof(Selected));

int main() {
	Box<double> first = 9.0;
	short small = 7;
	Box<short> direct(small);
	Box<double> second = 11.0;
	Partial<false, int*> partial = 13;
	short other = 15;
	Partial<true, short*> explicit_partial(other);
	return first.value == 9.0 && direct.value == 7 && second.value == 11.0 && partial.value == 13 && explicit_partial.value == 15 ? 0 : 1;
}
