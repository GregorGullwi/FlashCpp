struct PointerBox {
	int* value;

	PointerBox(int* pointer) : value(pointer) {}
};

struct LeftOperand {
	int operator+(PointerBox box) const {
		return box.value == nullptr ? 0 : 1;
	}
};

int accept(PointerBox box) {
	return box.value == nullptr ? 0 : 1;
}

int main() {
	return (LeftOperand{} + 0) + accept(0);
}
