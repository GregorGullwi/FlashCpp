struct Value {
private:
	int operator+() { return 1; }
};

int main() {
	Value value;
	return +value;
}
