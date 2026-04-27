struct ConstIntPtr {
	int data[2];

	operator const int*() const {
		return data;
	}
};

int main() {
	const ConstIntPtr ints = {{19, 23}};
	return ints[0] + 1[ints];
}
