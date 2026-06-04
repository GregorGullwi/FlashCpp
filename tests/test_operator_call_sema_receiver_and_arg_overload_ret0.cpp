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

int callConst(const ReceiverSensitiveCallable& callable) {
	return callable(2);
}

int main() {
	ReceiverSensitiveCallable receiver_sensitive;
	const ReceiverSensitiveCallable const_receiver_sensitive{};
	ArgSensitiveCallable arg_sensitive;

	int value = 3;

	if (receiver_sensitive(2) != 3) {
		return 1;
	}
	if (callConst(const_receiver_sensitive) != 12) {
		return 2;
	}
	if (arg_sensitive(value) != 103) {
		return 3;
	}
	if (arg_sensitive(4) != 204) {
		return 4;
	}

	return 0;
}
