struct Box {
	int value;

	Box(int v) : value(v) {}
};

int operator+(int lhs, const Box& rhs) {
	return lhs + rhs.value;
}

int main() {
	Box box(5);
	if ((1 + box) != 6)
		return 1;
	return 0;
}
