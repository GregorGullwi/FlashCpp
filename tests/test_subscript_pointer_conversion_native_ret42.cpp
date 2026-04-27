struct IntPtr {
	int data[4];

	operator int*() {
		return data;
	}
};

struct ShortPtr {
	short data[4];

	operator short*() {
		return data;
	}
};

int main() {
	IntPtr ints;
	ints.data[0] = 10;
	ints.data[1] = 20;

	ShortPtr shorts;
	shorts.data[2] = 12;

	return ints[0] + 1[ints] + 2[shorts];
}
