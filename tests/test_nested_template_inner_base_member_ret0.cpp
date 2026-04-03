// Regression test: nested template member function codegen must use the
// instantiated inner owner type so implicit `this` and inherited member lookup
// both work during deferred body generation.

template <typename T>
struct Wrapper {
	struct Base {
		int first;
	};

	struct Inner : Base {
		int read() const;
	};
};

template <typename T>
int Wrapper<T>::Inner::read() const {
	return this->first;
}

int main() {
	Wrapper<int>::Inner inner{};
	inner.first = 42;
	return inner.read() == 42 ? 0 : 1;
}
