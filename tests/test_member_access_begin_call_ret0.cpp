struct Container {
	int values[3];

	int* begin() { return &values[0]; }
};

struct Holder {
	Container container;
};

int main() {
	Holder holder{};
	holder.container.values[0] = 42;
	return *holder.container.begin() == 42 ? 0 : 1;
}
