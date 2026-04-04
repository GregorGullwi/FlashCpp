using __r = int;

struct Counter {
	int __r;

	void add(int value) {
		__r += value;
	}

	void sub(int value) {
		__r -= value;
	}

	int get() const {
		return __r;
	}
};

int main() {
	Counter counter{40};
	counter.add(5);
	counter.sub(3);
	return counter.get();
}
