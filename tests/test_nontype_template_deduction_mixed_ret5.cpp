template <typename T, int N>
struct Array {
	T data[N];
};

// T and N are deduced from the struct arg (pre-deduction pass),
// U is deduced directly from the second function argument.
template <typename T, int N, typename U>
U getElement(Array<T, N>& arr, U idx) { return idx; }

int main() {
	Array<int, 5> arr;
	return getElement(arr, 5);  // T=int, N=5 from arr; U=int from 5; returns 5
}
