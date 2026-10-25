struct Value {
private:
	int operator+() { return 3; }
	friend int read(Value value) { return +value; }
};

int main() {
	Value value;
	return read(value) + 4;
}
