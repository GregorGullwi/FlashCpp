template <typename T>
struct A {
	T value;
};

template <typename T>
struct B {
	T value;
};

template <typename T>
int f(A<T> x) {
	return 1;
}

int f(B<int> x) {
	return 7;
}

int main() {
	B<int> b{1};
	return f(b);
}
