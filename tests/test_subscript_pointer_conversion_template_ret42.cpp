template <typename T>
struct PtrBox {
	T data[3];

	operator T*() {
		return data;
	}
};

struct Small {
	int value;
};

int main() {
	PtrBox<int> ints;
	ints.data[1] = 20;

	PtrBox<Small> smalls;
	smalls.data[0].value = 22;

	PtrBox<int>* int_ptr = &ints;
	return 1[*int_ptr] + smalls[0].value;
}
