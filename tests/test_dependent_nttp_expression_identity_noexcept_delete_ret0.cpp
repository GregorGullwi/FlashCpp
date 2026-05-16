template <int N>
struct IntBox {
	static constexpr int value = N;
};

template <typename T>
struct NoexceptDeleteUse {
	static int first() { return IntBox<noexcept(delete (T*)0)>::value; }
	static int second() { return IntBox<noexcept(delete (T*)0) + 1>::value; }
};

int main() {
	int first_int = NoexceptDeleteUse<int>::first();
	int second_int = NoexceptDeleteUse<int>::second();
	int first_char = NoexceptDeleteUse<char>::first();
	int second_char = NoexceptDeleteUse<char>::second();

	return first_int != second_int &&
		   first_char != second_char &&
		   first_int == first_char &&
		   second_int == second_char ? 0 : 1;
}
