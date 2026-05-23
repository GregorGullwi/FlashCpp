struct Value {
	int v;
	explicit Value(int x) : v(x) {}

	friend bool operator==(Value lhs, int rhs) {
		return lhs.v == rhs;
	}

	friend bool operator==(Value lhs, Value rhs) {
		return lhs.v == rhs.v;
	}
};

int main() {
	Value value(0);
	return (value == 0) ? 0 : 1;
}
