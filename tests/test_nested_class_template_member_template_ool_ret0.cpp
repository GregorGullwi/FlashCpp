template<typename T>
struct Outer {
	template<typename U>
	struct Inner {
		template<unsigned long long N>
		int f(int (&arr)[N]);
	};
};

template<typename T>
template<typename U>
template<unsigned long long N>
int Outer<T>::Inner<U>::f(int (&arr)[N]) {
	return static_cast<int>(N + arr[0]);
}

int main() {
	Outer<int>::Inner<double> v;
	int values[3] = {4, 0, 0};
	return v.f(values) == 7 ? 0 : 1;
}
