template <typename T, typename U = int>
struct box {
	int value = sizeof(U) == 4 ? 42 : 0;
};

int main() {
	box<char> named;
	return named.value + box<char, int>().value;
}
