int main() {
	int values[4] = {1, 2, 3, 4};
	int* cursor = values + 1;

	int* advanced = __atomic_add_fetch(&cursor, 2LL * sizeof(int), __ATOMIC_SEQ_CST);
	int* prior = __atomic_fetch_sub(&cursor, 1LL * sizeof(int), __ATOMIC_SEQ_CST);

	return advanced == values + 3 &&
			prior == values + 3 &&
			cursor == values + 2
		? 0
		: 1;
}
