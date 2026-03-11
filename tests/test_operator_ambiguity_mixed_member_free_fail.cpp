// Mixed incomparable member/free candidates must be ambiguous.

struct Box {
	int value;

	Box(int v) : value(v) {}

	operator int() const {
		return value;
	}

	int operator+(int rhs) const {
		return value + rhs;
	}
};

int operator+(int lhs, Box rhs) {
	return lhs + rhs.value;
}

int main() {
	Box box(11);
	return box + box;
}