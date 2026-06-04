// Regression: dependent callable-object operator() resolution in sema must
// preserve receiver constness and lvalue/rvalue argument categories after
// template substitution.

struct ReceiverSensitiveCallable {
	int operator()(int value) {
		return value + 1;
	}

	int operator()(int value) const {
		return value + 10;
	}
};

struct ArgSensitiveCallable {
	int operator()(int& value) const {
		return value + 100;
	}

	int operator()(int&& value) const {
		return value + 200;
	}
};

template <typename Callable>
int callConst(const Callable& callable) {
	return callable(2);
}

template <typename Callable>
int callArgSensitive(const Callable& callable) {
	int value = 3;
	return callable(value) + callable(4);
}

int main() {
	ReceiverSensitiveCallable receiver_sensitive;
	ArgSensitiveCallable arg_sensitive;

	if (callConst(receiver_sensitive) != 12) {
		return 1;
	}
	if (callArgSensitive(arg_sensitive) != 307) {
		return 2;
	}

	return 0;
}
