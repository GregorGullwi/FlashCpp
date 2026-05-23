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
	(void)arr;
	return static_cast<int>(N);
}

int main() {
	Outer<int>::Inner<double> v;
	(void)v;
	return 0;
}
