struct PointerBox {
	PointerBox(int*) {}
};

int main() {
	PointerBox box(1);
	return 0;
}
