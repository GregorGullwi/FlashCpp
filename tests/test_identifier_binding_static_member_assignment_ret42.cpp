struct Counter {
	static int count;

	void bump() {
		count = 40;
		++count;
		count += 1;
	}
};

int Counter::count = 0;

int main() {
	Counter counter;
	counter.bump();
	return Counter::count;
}
