struct Container {
	int values[3];

	int* begin() { return &values[0]; }
	int* end() { return &values[3]; }
};

Container makeContainer() {
	Container c{};
	c.values[0] = 1;
	c.values[1] = 2;
	c.values[2] = 3;
	return c;
}

int main() {
	int sum = 0;
	for (auto value : makeContainer()) {
		sum += value;
	}
	return sum == 6 ? 0 : 1;
}
