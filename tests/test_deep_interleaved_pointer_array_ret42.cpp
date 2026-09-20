int main() {
	int *(*(*q)[2])[3][5] = nullptr;
	return sizeof(*q) == 2 * sizeof(int (*)[3][5]) ? 42 : 1;
}
