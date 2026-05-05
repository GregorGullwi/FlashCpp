template <class T>
struct Source {
	static T make() {
		return T{};
	}
};

template <class T>
struct Sink {
	template <class U>
	static int accept(U value) {
		(void)value;
		return 0;
	}

	static int run() {
		return Sink::accept(Source<T>::make());
	}
};

int main() {
	return Sink<int>::run();
}
