struct Value {
private:
	int operator+() { return 1; }
};

using Result = decltype(+Value{});
