struct Pad {
	char byte;
};

template <class T>
struct Base {
	T value;
};

template <class T>
struct Derived : Pad, Base<T> {
	short tag;
};

template <class T>
struct Source {
	Derived<T> derived;
	operator Derived<T>() const { return derived; }
};

long long read(Base<long long> object) {
	return object.value;
}

int main() {
	Source<long long> source;
	source.derived.value = 42;
	source.derived.tag = 7;
	if (read(source) != 42)
		return 1;
	if (source.derived.tag != 7)
		return 2;
	return 0;
}
