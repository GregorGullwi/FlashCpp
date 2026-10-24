// C++20 [temp.type]: a declarator written around a template parameter is part
// of the substituted type. Hold<T (*[2])[4]> with T = int denotes
// Hold<int (*[2])[4]>. A pointer written on the pattern around that array
// stays a pointer to the array object.
template<class U>
struct Hold {
	U value;
};

template<class T>
struct Wrap {
	Hold<T (*[2])[4]> inner;
};

template<class T>
struct Ptr {
	T* value;
};

int storage = 7;

int main() {
	if (sizeof(Wrap<int>{}.inner.value) != 2 * sizeof(int*)) {
		return 1;
	}
	if (sizeof(Wrap<int>{}.inner.value[0]) != sizeof(int*)) {
		return 2;
	}
	if (sizeof(*Wrap<int>{}.inner.value[0]) != 4 * sizeof(int)) {
		return 3;
	}
	if (sizeof(Ptr<int (*[2])[4]>{}.value) != sizeof(int*)) {
		return 4;
	}
	if (sizeof(*Ptr<int (*[2])[4]>{}.value) != 2 * sizeof(int*)) {
		return 5;
	}

	Ptr<int> scalar{};
	scalar.value = &storage;
	if (*scalar.value != 7) {
		return 6;
	}
	return 0;
}
