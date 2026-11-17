struct VolatileDoubleDerefPoint {
	int x;
};

int main() {
	VolatileDoubleDerefPoint point = {7};
	VolatileDoubleDerefPoint* pointer = &point;
	VolatileDoubleDerefPoint* volatile* slot = &pointer;
	return (*slot)->x == 7 ? 0 : 1;
}
