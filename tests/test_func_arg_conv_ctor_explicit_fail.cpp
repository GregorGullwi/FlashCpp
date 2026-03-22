struct Box {
	int value;

	explicit Box(int v) : value(v) {}
};

int take(Box b) {
	return b.value;
}

int main() {
	return take(7);
}
