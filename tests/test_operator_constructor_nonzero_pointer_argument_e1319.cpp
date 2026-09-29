struct PointerBox {
	PointerBox(int*) {}
};

struct LeftOperand {
	int operator+(PointerBox) const {
		return 0;
	}
};

int main() {
	return LeftOperand{} + 1;
}
