struct Base {
protected:
	int operator+() { return 3; }
};
struct Value : Base {
	int read() { return +*this; }
};

int main() {
	Value value;
	return value.read();
}
