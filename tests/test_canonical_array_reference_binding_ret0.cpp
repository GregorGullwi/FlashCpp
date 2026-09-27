int selectArray(int (&values)[5]) {
	return values[4];
}

int selectArray(const int (&)[5]) {
	return -1;
}

int selectArray(const int (&)[4]) {
	return -2;
}

int main() {
	int values[5] = {1, 2, 3, 4, 42};
	return selectArray(values) == 42 ? 0 : 1;
}
