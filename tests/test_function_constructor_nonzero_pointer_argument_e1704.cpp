struct PointerBox {
	PointerBox(int*) {}
};

int accept(PointerBox) {
	return 0;
}

int main() {
	return accept(1);
}
