constexpr decltype(0) global_values[] = {1, 2, 3};

constexpr int readValues() {
	constexpr decltype(0) local_values[] = {4, 5, 6};
	return global_values[1] + local_values[0];
}

int main() {
	return readValues() - 6;
}