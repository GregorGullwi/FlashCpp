struct Value {
	int data;

	friend bool operator==(const Value&, const Value&) = default;
};

int main() {
	Value low{1};
	Value same{1};
	Value high{3};

	if (!(low == same)) {
		return 1;
	}
	if (low == high) {
		return 2;
	}
	return 0;
}
