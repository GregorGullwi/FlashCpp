// Regression test: delayed member function body using 'this' pointer
// Verifies that 'this' injection works correctly in delayed parsing path.

struct Counter {
	int count;

	Counter(int v) : count(v) {}

	int get() { return this->count; }
	void add(int n) { this->count += n; }
};

int main() {
	Counter c(10);
	if (c.get() != 10)
		return 1;
	c.add(5);
	if (c.get() != 15)
		return 2;
	c.add(3);
	if (c.get() != 18)
		return 3;
	return 0;
}
