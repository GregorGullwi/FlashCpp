int main() {
	int values[3] = {10, 20, 30};
	int* ptr = values;
	ptr += 2;
	return *ptr;
}
