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
		: payload(0) {}
};

struct Sink {
	int value;

	Sink(Payload&)
		: value(1) {}

	Sink(Payload&&)
		: value(41) {}
};

Wrapper makeWrapper() {
	return Wrapper();
}

int main() {
	Wrapper wrapper;
	Sink from_lvalue(wrapper.payload);
	if (from_lvalue.value != 1) return 1;
	Sink from_xvalue(makeWrapper().payload);
	if (from_xvalue.value != 41) return 2;
	return 0;
}
