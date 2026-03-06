struct Counter {
	int value;
	int run() {
		auto f = [this]() { return value - value; };
		return f();
	}
};

int main() {
	Counter c;
	c.value = 42;
	return c.run();
}
