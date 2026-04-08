struct Container {
	int values[4];

	int* begin() { return &values[0]; }
	int* end() { return &values[4]; }
};

Container makeContainer(int base) {
	Container c{};
	c.values[0] = base;
	c.values[1] = base + 1;
	c.values[2] = base + 2;
	c.values[3] = base + 3;
	return c;
}

int main() {
	int sum = 0;
	for (int factor = 2; auto value : makeContainer(4)) {
		sum += value * factor;
	}

	return sum == ((4 + 5 + 6 + 7) * 2) ? 0 : 1;
}
