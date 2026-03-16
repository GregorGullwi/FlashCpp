struct Container {
	int values[3];

	int* begin() { return &values[0]; }
	int* end() { return &values[3]; }
};

struct Holder {
	Container container;
};

int main() {
	Holder holder{};
	holder.container.values[0] = 1;
	holder.container.values[1] = 2;
	holder.container.values[2] = 3;

	int sum = 0;
	for (auto value : holder.container) {
		sum += value;
	}
	return sum == 6 ? 0 : 1;
}
