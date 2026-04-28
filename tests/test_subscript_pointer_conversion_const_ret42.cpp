struct ConstIntPtr {
	int data[2];

	operator const int*() const {
		return &data[0];
	}
};

int main() {
	const ConstIntPtr ints = {{19, 23}};
	return ints[0] + 1[ints];
}
