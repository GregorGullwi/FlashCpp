struct reordering {
	char value;

	explicit reordering(int input) : value(static_cast<char>(input)) {}
};

int main() {
	reordering x = 1;
	return x.value;
}
