struct TripleDerefPoint {
	int x;
};

int main() {
	TripleDerefPoint point = {7};
	TripleDerefPoint* pointer = &point;
	TripleDerefPoint** slot = &pointer;
	TripleDerefPoint*** outer = &slot;
	return (**outer)->x == 7 ? 0 : 1;
}
