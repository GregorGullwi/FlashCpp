struct Value {
	int data;

	friend bool operator==(const Value&, const Value&) = delete;
};

int main() {
	Value lhs{1};
	Value rhs{2};
	return lhs == rhs;
}
