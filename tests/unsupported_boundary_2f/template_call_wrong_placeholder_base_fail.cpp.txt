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
	return x.value;
}

int main() {
	B<int> b{1};
	return f(b);
}
