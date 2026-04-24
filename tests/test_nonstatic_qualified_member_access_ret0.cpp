struct Counter {
	int value = 21;

	int read() const {
		return Counter::value;
	}
};

int main() {
	Counter counter;
	counter.value = 42;
	return counter.read() - 42;
}
