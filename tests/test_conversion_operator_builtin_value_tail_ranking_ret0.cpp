template<class T> struct Source {
	T value;
	operator T() const { return value; }
	operator long long() const { return 99; }
};

int read(int value) {
	return value;
}

int main() {
	const Source<short> source{42};
	return read(source) == 42 ? 0 : 1;
}
