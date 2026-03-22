struct Source {
	int value;

	Source(int x) : value(x) {}
};

struct Target {
	int value;

	Target(Source s, int bias = 2) : value(s.value + bias) {}
};

int consume(Target t) {
	return t.value;
}

int main() {
	Source source = 40;
	Source other = 38;
	return consume(source) + consume(other) - 40;
}
