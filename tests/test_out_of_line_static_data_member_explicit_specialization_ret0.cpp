// An explicit `template<>` specialization of a static data member may be
// defined out of line with its own initializer, including an array member. The
// definition completes the specialization's in-class declaration instead of the
// primary template's storage.

template <typename T>
struct S {
	static int data;
	static int values[2];
};

template <>
int S<int>::data = 4;
template <>
int S<int>::values[2] = {4, 5};

int main() {
	if (S<int>::data != 4) {
		return 1;
	}
	if (S<int>::values[0] != 4 || S<int>::values[1] != 5) {
		return 2;
	}

	S<int>::data = 40;
	S<int>::values[1] = 41;
	if (S<int>::data != 40 || S<int>::values[1] != 41) {
		return 3;
	}
	return 0;
}
