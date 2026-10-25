struct Value {
	int operator+() & = delete;
	int operator+() && { return 4; }
};

int main() {
	return +Value{};
}
