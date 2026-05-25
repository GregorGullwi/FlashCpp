struct Sink {
	int value;
	Sink(int v)
		: value(v) {}
};

template<typename T>
struct Box {
	static auto plusOne(T v);

	static int consume(T v) {
		Sink s = plusOne(v);
		return s.value;
	}
};

template<typename T>
auto Box<T>::plusOne(T v) {
	return v + 1;
}

int main() {
	return Box<int>::consume(41) == 42 ? 0 : 1;
}
