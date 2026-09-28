int select(int*&& values) {
	return values[0] + values[1];
}

int select(const int*&& values) {
	return -1;
}

int main() {
	int values[2] = {20, 22};
	return select(values) == 42 ? 0 : 1;
}
