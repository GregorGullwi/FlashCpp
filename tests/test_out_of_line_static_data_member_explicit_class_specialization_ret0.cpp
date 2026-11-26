// An out-of-line definition of a static member of an explicitly specialized
// class template completes the specialization's declaration. The array member
// has the same `int[2]` shape in both declarations, so it must bind instead of
// reporting a type conflict. Both members are written and read.

template <class T>
struct S {
	static int data;
	static int values[2];
};

template <>
struct S<int> {
	static int data;
	static int values[2];
};

int S<int>::data = 3;
int S<int>::values[2] = {5, 7};

int main() {
	if (S<int>::data != 3) {
		return 1;
	}
	if (S<int>::values[0] != 5 || S<int>::values[1] != 7) {
		return 2;
	}

	S<int>::data = 11;
	S<int>::values[0] = 13;
	S<int>::values[1] = 17;
	if (S<int>::data != 11) {
		return 3;
	}
	if (S<int>::values[0] != 13 || S<int>::values[1] != 17) {
		return 4;
	}
	return 0;
}
