typedef unsigned long size_t;

template<typename Type, size_t N>
int arraySize(Type (&)[N]) {
	return static_cast<int>(N);
}

int main() {
	int values[4] = {1, 2, 3, 4};
	return arraySize(values) == 4 ? 0 : 1;
}
