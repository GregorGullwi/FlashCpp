struct Target {
	int value;

	explicit Target(int x) : value(x + 1000) {}
	Target(double d, int bias = 2) : value(static_cast<int>(d) + bias) {}
};

struct Sink {
	int consume(Target t) {
		return t.value;
	}
};

int main() {
	Sink sink;
	int source = 40;
	return sink.consume(source) + sink.consume(38) - 40;
}
