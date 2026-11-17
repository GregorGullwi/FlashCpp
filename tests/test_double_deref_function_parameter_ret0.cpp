struct FunctionDoubleDerefPoint {
	int x;
};

int read_slot(FunctionDoubleDerefPoint** slot) {
	return (*slot)->x;
}

int main() {
	FunctionDoubleDerefPoint point = {7};
	FunctionDoubleDerefPoint* pointer = &point;
	return read_slot(&pointer) == 7 ? 0 : 1;
}
