template<class T>
struct Multi {
	template<class U>
	int eval(T left, U right);

	template<class U>
	int eval(U a, U b, U c);
};

template<class T>
template<class U>
int Multi<T>::eval(T left, U right) {
	return left + right;
}

template<class T>
template<class U>
int Multi<T>::eval(U a, U b, U c) {
	return a + b + c;
}

int main() {
	Multi<int> m;
	if (m.eval(40, 2) != 42) {
		return 1;
	}
	if (m.eval(10, 20, 12) != 42) {
		return 2;
	}
	return 0;
}
