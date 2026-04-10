struct Box {
	int value;

	Box(int x) : value(x) {}
};

int main() {
	int source = 42;
	int* ptr = &source;
	Box box(*ptr);
	return box.value - 42;
}
