struct ConditionalDoubleDerefPoint {
	int x;
};

int main() {
	ConditionalDoubleDerefPoint point = {7};
	ConditionalDoubleDerefPoint* pointer = &point;
	ConditionalDoubleDerefPoint** slot = &pointer;
	ConditionalDoubleDerefPoint** other = &pointer;
	return (* (true ? slot : other))->x == 7 ? 0 : 1;
}
