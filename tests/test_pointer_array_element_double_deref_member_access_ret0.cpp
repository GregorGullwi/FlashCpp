struct PointerArrayDoubleDerefPoint {
	int x;
	int y;
};

int main() {
	PointerArrayDoubleDerefPoint first = {1, 2};
	PointerArrayDoubleDerefPoint second = {3, 4};
	PointerArrayDoubleDerefPoint* points[2] = {&first, &second};
	int index = 1;
	PointerArrayDoubleDerefPoint** slot = &points[index];
	return (*slot)->x + (*slot)->y == 7 ? 0 : 1;
}
