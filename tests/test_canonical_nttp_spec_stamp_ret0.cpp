// Literal NTTP class-template specializations stamp opaque ExprIds onto Spec
// identity so Hold<3> and Hold<true> remain distinct through canonical import.
template <int N>
struct Hold {
	static constexpr int value = N;
};

template <bool B>
struct Flag {
	static constexpr int value = B ? 4 : 1;
};

template <typename T, int N>
struct Mix {
	T head;
	static constexpr int count = N;
};

int main() {
	Mix<short, 3> mixed{};
	mixed.head = 2;
	return Hold<3>::value + Flag<true>::value + mixed.head + Mix<short, 3>::count -
		(3 + 4 + 2 + 3);
}
