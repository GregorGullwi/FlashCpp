struct Box {
	int value;

	explicit Box(int v) : value(v) {}
};

Box make() {
	return 7;
}

int main() {
	Box box = make();
	return box.value;
}
