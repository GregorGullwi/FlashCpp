template<typename T, int N>
struct array_traits {
	using type = T[N];
};

template<typename T, int N>
struct wrapper {
	typename array_traits<T, N>::type elems;
	int tail;
};

int main() {
	wrapper<int, 3> value = {1, 2, 3, 7};
	return (value.elems[0] == 1 &&
			value.elems[1] == 2 &&
			value.elems[2] == 3 &&
			value.tail == 7)
			   ? 0
			   : 1;
}
