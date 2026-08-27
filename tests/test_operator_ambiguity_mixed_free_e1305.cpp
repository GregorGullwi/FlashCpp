// Mixed incomparable free-function operator candidates must be ambiguous.

struct Box {
	int value;

	Box(int v) : value(v) {}

	operator int() const {
		return value;
	}
};

int operator+(Box lhs, int rhs) {
	return lhs.value + rhs;
}

int operator+(int lhs, Box rhs) {
	return lhs + rhs.value;
}

int main() {
	Box box(7);
	return box + box;
}