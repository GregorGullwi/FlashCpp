namespace lib {
	struct X {};
}

template <class T>
int call(T value) {
	return g(value);
}

namespace lib {
	template <class U>
	int g(U) {
		return 42;
	}
}

int main() {
	lib::X x{};
	return call(x) - 42;
}
