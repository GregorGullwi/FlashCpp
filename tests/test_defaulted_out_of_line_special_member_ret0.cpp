// Out-of-line '= default' definitions of special member functions are valid:
// a plain constructor and copy assignment, and a constructor of a class
// template. The definitions must materialize and run.
struct Plain {
	int value;

	Plain();
	Plain& operator=(Plain const&);
};

Plain::Plain() = default;
Plain& Plain::operator=(Plain const&) = default;

template <typename T>
struct Box {
	T value;

	Box();
};

template <typename T>
Box<T>::Box() = default;

int main() {
	Plain a;
	a.value = 4;
	Plain b;
	b.value = 0;
	b = a;
	if (b.value != 4) {
		return 1;
	}

	Box<short> boxed;
	boxed.value = 6;
	if (boxed.value != 6) {
		return 2;
	}
	return 0;
}
