struct Counter {
	Counter operator++(int = 0) {
		return *this;
	}
};

int main() {
	Counter counter;
	counter++;
	return 0;
}
