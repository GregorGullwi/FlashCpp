struct PointerArrayPoint {
	int x;
	int y;
};

int main() {
	PointerArrayPoint first = {1, 2};
	PointerArrayPoint second = {3, 4};
	PointerArrayPoint* points[2] = {&first, &second};
	int index = 1;
	return points[index]->x + points[index]->y == 7 ? 0 : 1;
}
