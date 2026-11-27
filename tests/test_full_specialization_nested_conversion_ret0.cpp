template<class T>
struct Outer {};

template<>
struct Outer<int> {
	struct Inner {
		int value;
		template<class U>
		operator U&() { return value; }
	};
};

template<>
struct Outer<long long> {
	struct Inner {
		long long prefix;
		int value;
		template<class U>
		operator U&() { return value; }
	};
};

int read(const Outer<int>::Inner& inner) { return inner.value; }
int read(const Outer<long long>::Inner& inner) { return inner.value + 100; }

int main() {
	Outer<int>::Inner narrow{3};
	Outer<long long>::Inner wide{7, 11};
	int& narrow_reference = narrow;
	int& wide_reference = wide;
	narrow_reference = 17;
	wide_reference = 19;
	return read(narrow) == 17 && read(wide) == 119 ? 0 : 1;
}
