struct Box {
	int value;

	Box(int x) : value(x) {}
};

int main() {
	int paren_value = Box(7).value;
	int brace_value = Box{5}.value;

	if (paren_value != 7) return 1;
	if (brace_value != 5) return 2;

	return 0;
}
