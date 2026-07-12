template<typename... Ts>
struct Types {};

template<typename Packed, typename Tail>
struct SelectTail;

template<typename... Elements, typename Tail>
struct SelectTail<Types<Elements...>, Tail> {
	using type = Tail;
};

struct Large {
	long long values[3];
};

int main() {
	using NonEmptyTail = SelectTail<Types<char, short, int>, Large>::type;
	using EmptyTail = SelectTail<Types<>, long long>::type;
	return sizeof(NonEmptyTail) == sizeof(Large) &&
		sizeof(EmptyTail) == sizeof(long long)
		? 0
		: 1;
}
