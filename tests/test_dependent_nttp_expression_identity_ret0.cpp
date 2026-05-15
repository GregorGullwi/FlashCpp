template <int N>
struct Box {
	static constexpr int value = N;
};

template <typename T>
struct UseDependentExprs {
	static int first() { return Box<sizeof(T)>::value; }
	static int second() { return Box<sizeof(T) + 1>::value; }
};

struct MixedPayload {
	double d;
	char c;
};

int main() {
	int first_int = UseDependentExprs<int>::first();
	int second_int = UseDependentExprs<int>::second();
	int first_char = UseDependentExprs<char>::first();
	int second_char = UseDependentExprs<char>::second();
	int first_struct = UseDependentExprs<MixedPayload>::first();
	int second_struct = UseDependentExprs<MixedPayload>::second();
	return first_int == (int)sizeof(int) &&
		   second_int == ((int)sizeof(int) + 1) &&
		   first_int != second_int &&
		   first_char == (int)sizeof(char) &&
		   second_char == ((int)sizeof(char) + 1) &&
		   first_char != second_char &&
		   first_struct == (int)sizeof(MixedPayload) &&
		   second_struct == ((int)sizeof(MixedPayload) + 1) &&
		   first_struct != second_struct ? 0 : 1;
}
