struct Box {
	int value;

	Box(int v) : value(v) {}

	int operator+(int& rhs) const {
		return value + rhs;
	}

	int operator+(int&& rhs) const {
		return value + rhs + 100;
	}
};

int main() {
	Box box(5);
	int value = 7;
	if ((box + value) != 12) return 1;
	if ((box + 3) != 108) return 2;
	return 0;
}
