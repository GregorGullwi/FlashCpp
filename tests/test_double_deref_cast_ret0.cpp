struct CastDoubleDerefPoint {
	int x;
};

int main() {
	CastDoubleDerefPoint point = {7};
	CastDoubleDerefPoint* pointer = &point;
	void** raw = reinterpret_cast<void**>(&pointer);
	return (*reinterpret_cast<CastDoubleDerefPoint**>(raw))->x == 7 ? 0 : 1;
}
