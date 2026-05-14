namespace A {
	template <class T>
	int f(T) {
		return 42;
	}

	template <class T>
	int call() {
		return f(0);
	}
}

namespace B {
	template <class T>
	int f(int) {
		return 7;
	}
}

int main() {
	return A::call<int>();
}
