// C++20 [temp.arg.type]: a type-id template argument denotes that type.
// int (*[2])[4] is an array of two pointers to int[4]. The stored argument
// keeps that declarator, and so does a pointer to the array object.
// Member subscript lowering still reads flat pointer/array fields, so the
// stored shape is observed with unevaluated sizeof.
template<class T>
struct Hold {
	T value;
};

int storage = 7;

int main() {
	if (sizeof(Hold<int (*[2])[4]>{}.value) != 2 * sizeof(int*)) {
		return 1;
	}
	if (sizeof(Hold<int (*[2])[4]>{}.value[0]) != sizeof(int*)) {
		return 2;
	}
	if (sizeof(*Hold<int (*[2])[4]>{}.value[0]) != 4 * sizeof(int)) {
		return 3;
	}
	if (sizeof(Hold<int (*(*)[2])[4]>{}.value) != sizeof(int*)) {
		return 4;
	}
	if (sizeof(*Hold<int (*(*)[2])[4]>{}.value) != 2 * sizeof(int*)) {
		return 5;
	}

	Hold<int*> scalar{};
	scalar.value = &storage;
	if (*scalar.value != 7) {
		return 6;
	}
	return 0;
}
