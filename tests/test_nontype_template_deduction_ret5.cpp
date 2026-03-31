// Test: non-type template parameter deduction from a struct template instantiation.
// template<typename T, int N> int getSize(Array<T,N>& arr) is called as getSize(arr)
// where arr has type Array<int,5>.  The compiler must deduce T=int and N=5 and
// substitute N in the body, returning 5.

template <typename T, int N>
struct Array {
	T data[N];
	int size() const { return N; }
};

template <typename T, int N>
int getSize(Array<T, N>& arr) {
	return N;
}

int main() {
	Array<int, 5> arr;
	return getSize(arr);
}
