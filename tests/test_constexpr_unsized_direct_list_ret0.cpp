constexpr int global_values[]{1, 2, 3};

constexpr int readLocalValues() {
	int local_values[]{4, 5, 6};
	return local_values[0] + local_values[2];
}

static_assert(global_values[1] == 2);
static_assert(readLocalValues() == 10);

int main() {
	if (global_values[0] != 1 || global_values[2] != 3) {
		return 1;
	}
	if (readLocalValues() != 10) {
		return 2;
	}
	return 0;
}
