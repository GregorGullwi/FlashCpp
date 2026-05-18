constexpr int select_overload(long) {
	return 17;
}

template <typename T>
struct Holder {
	static const int value;
};

template <typename T>
// Two-phase lookup: unqualified lookup for select_overload happens at template definition
// time, when only select_overload(long) is visible, so both instantiations evaluate to 17.
constexpr int Holder<T>::value = select_overload(T{});

constexpr int select_overload(int) {
	return 23;
}

int main() {
	static_assert(Holder<int>::value == 17);
	static_assert(Holder<long>::value == 17);
	return 0;
}
