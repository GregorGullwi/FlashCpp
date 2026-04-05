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
	Sink(Payload&&)
		: value(2) {}
};

Wrapper makeWrapper() {
	return Wrapper();
}

int main() {
	Wrapper wrapper;
	Sink lvalue_sink(wrapper.payload);
	if (lvalue_sink.value != 1)
		return 1;

	Sink xvalue_sink(makeWrapper().payload);
	return xvalue_sink.value == 2 ? 0 : 2;
}
