struct Container {
	int values[3];

	int* begin() { return &values[0]; }
	int* end() { return &values[3]; }
};

struct Holder {
	Container container;
};

Holder holder;
int calls = 0;

Holder& get_holder() {
	++calls;
	return holder;
}

int main() {
	holder.container.values[0] = 1;
	holder.container.values[1] = 2;
	holder.container.values[2] = 3;

	int sum = 0;
	for (int value : get_holder().container) {
		sum += value;
	}

	return (sum == 6 && calls == 1) ? 0 : 1;
}
