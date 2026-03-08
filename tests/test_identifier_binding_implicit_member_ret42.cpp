struct Counter {
	int value;

	int addTwo() {
		int delta = 2;
		return value + delta;
	}
};

int main() {
	Counter counter{40};
	return counter.addTwo();
}
