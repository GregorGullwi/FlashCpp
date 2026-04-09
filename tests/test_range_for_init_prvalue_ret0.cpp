struct Container {
	int values[3];

	int* begin() { return &values[0]; }
	int* end() { return &values[3]; }
};

Container makeContainer(int base) {
	Container c{};
	c.values[0] = base;
	c.values[1] = base + 1;
	c.values[2] = base + 2;
	return c;
}

int main() {
	int sum = 0;

	for (int factor = 2; auto value : makeContainer(1)) {
		sum += value * factor;
	}

	for (int factor = 3; auto value : makeContainer(4)) {
		sum += value * factor;
	}

	int factor = 5;
	sum += factor;

	return sum == 62 ? 0 : 1;
}
