struct Value {
private:
	int operator+() { return 3; }
	friend int read(Value value) { return sizeof(decltype(+value)); }
};

int main() {
	Value value;
	return read(value) == sizeof(int) ? 0 : 1;
}
