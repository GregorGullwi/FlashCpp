template<typename T, int N>
struct row_alias {
	using type = T[N];
};

template<typename T, int N, int M>
struct matrix_wrapper {
	using row = typename row_alias<T, N>::type;
	using matrix = row[M];

	matrix values;
};

int check_row(int (&)[3]) {
	return 0;
}

int check_row(int (&)[2]) {
	return 1;
}

int main() {
	matrix_wrapper<int, 3, 2> value = {};
	return check_row(value.values[0]);
}
