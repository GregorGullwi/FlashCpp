struct Value {
	int operator+() = delete;
};

using Result = decltype(+Value{});
