struct DoubleDerefPoint {
	int x;
	int y;
};

int main() {
	DoubleDerefPoint point = {3, 4};
	DoubleDerefPoint* pointer = &point;
	DoubleDerefPoint** slot = &pointer;
	return (*slot)->x + (*slot)->y == 7 ? 0 : 1;
}
