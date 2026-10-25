struct Value {
	int operator+() = delete;
};

int main() {
	Value value;
	return +value;
}
