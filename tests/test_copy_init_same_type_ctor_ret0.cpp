struct ExplicitIntBox {
	explicit ExplicitIntBox(int v) : value(v) {}

	int value;
};

ExplicitIntBox makeBox() {
	return ExplicitIntBox(42);
}

int main() {
	ExplicitIntBox local = ExplicitIntBox(7);
	return local.value == 7 && makeBox().value == 42 ? 0 : 1;
}
