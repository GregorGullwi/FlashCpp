struct CallDoubleDerefPoint {
	int x;
};

CallDoubleDerefPoint** get_slot(CallDoubleDerefPoint*& pointer) {
	return &pointer;
}

int main() {
	CallDoubleDerefPoint point = {7};
	CallDoubleDerefPoint* pointer = &point;
	return (*get_slot(pointer))->x == 7 ? 0 : 1;
}
