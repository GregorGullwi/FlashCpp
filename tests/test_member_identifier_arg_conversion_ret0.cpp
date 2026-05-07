struct Box {
	int value;

	Box(double input, int bias = 2) : value(static_cast<int>(input) + bias) {}
};

struct Sink {
	int takeBox(Box box) {
		return box.value;
	}

	int takeDouble(double value) {
		return static_cast<int>(value * 2.0);
	}
};

int main() {
	Sink sink;

	int int_source = 40;
	if (sink.takeBox(int_source) != 42)
		return 1;

	float float_source = 1.5f;
	if (sink.takeDouble(float_source) != 3)
		return 2;

	return 0;
}
