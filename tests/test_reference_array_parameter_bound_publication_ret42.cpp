// An out-of-line member template parameter of type int (&)[N] keeps its
// reference-to-array shape after N is substituted. The instantiated parameter
// type must publish the resolved array bound so codegen identifies the array
// address instead of dereferencing the bare T&.
template<typename T>
struct Outer {
	template<unsigned long long N>
	int sum_first(int (&rows)[N]);
};

template<typename T>
template<unsigned long long N>
int Outer<T>::sum_first(int (&rows)[N]) {
	return rows[0] + static_cast<int>(N);
}

int main() {
	int values[3] = {4, 0, 0};
	Outer<int> outer;
	return outer.sum_first(values) == 7 ? 42 : 1;
}
