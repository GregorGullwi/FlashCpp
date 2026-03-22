struct Target {
	int value;

	explicit Target(int x) : value(x + 1000) {}
	Target(double d, int bias = 2) : value(static_cast<int>(d) + bias) {}
};

int main() {
	int source = 40;
	Target from_ident = source;
	Target from_literal = 38;
	return from_ident.value + from_literal.value - 40;
}
