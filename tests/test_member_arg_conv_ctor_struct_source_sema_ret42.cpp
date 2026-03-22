struct Source {
	int value;

	Source(int x) : value(x) {}
};

struct Target {
	int value;

	Target(Source s, int bias = 2) : value(s.value + bias) {}
};

struct Sink {
	int consume(Target t) {
		return t.value;
	}
};

int main() {
	Sink sink;
	Source source = 40;
	return sink.consume(source) + sink.consume(Source(38)) - 40;
}
