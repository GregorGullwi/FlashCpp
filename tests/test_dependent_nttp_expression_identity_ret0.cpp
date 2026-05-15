template <int N>
struct Box {
	static constexpr int value = N;
};

template <typename T>
struct UseDependentExprs {
	static int first() { return Box<sizeof(T)>::value; }
	static int second() { return Box<sizeof(T) + 1>::value; }
};

int main() {
	int first = UseDependentExprs<int>::first();
	int second = UseDependentExprs<int>::second();
	return first == (int)sizeof(int) && second == ((int)sizeof(int) + 1) && first != second ? 0 : 1;
}
