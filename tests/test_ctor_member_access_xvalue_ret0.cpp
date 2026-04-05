// Test: prvalue member access should propagate xvalue category into sema-backed
// constructor overload resolution.
struct Payload {
	int value;

	Payload(int v)
		: value(v) {}

	Payload(const Payload&) = delete;
	Payload(Payload&& other)
		: value(other.value) {}
};

struct Wrapper {
	Payload payload;

	Wrapper()
		: payload(42) {}
};

struct Sink {
	int value;

	Sink(Payload&)
		: value(1) {}
	Sink(Payload payload)
		: value(payload.value - 42) {}
};

Wrapper makeWrapper() {
	return Wrapper();
}

int main() {
	Sink sink(makeWrapper().payload);
	return sink.value;
}
