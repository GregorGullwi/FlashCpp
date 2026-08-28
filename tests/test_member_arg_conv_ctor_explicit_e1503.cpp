struct Box {
	int value;

	explicit Box(int v) : value(v) {}
};

struct Sink {
	int take(Box b) {
		return b.value;
	}
};

int main() {
	Sink sink;
	return sink.take(7);
}
