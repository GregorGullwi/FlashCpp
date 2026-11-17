struct InterleavedDoubleDerefPoint {
	int x;
};

int main() {
	InterleavedDoubleDerefPoint point = {7};
	InterleavedDoubleDerefPoint* pointer = &point;
	InterleavedDoubleDerefPoint** slots[1] = {&pointer};
	InterleavedDoubleDerefPoint** (*grid)[1] = &slots;
	return (*(*grid)[0])->x == 7 ? 0 : 1;
}
