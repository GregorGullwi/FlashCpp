struct ReceiverSensitiveSubscript {
	int value;

	int& operator[](int) {
		return value;
	}

	int operator[](int) const {
		return value + 10;
	}
};

struct ArgSensitiveSubscript {
	int operator[](int& index) const {
		return index + 100;
	}

	int operator[](int&& index) const {
		return index + 200;
	}
};

int readConst(const ReceiverSensitiveSubscript& subscriptable) {
	return subscriptable[0];
}

int main() {
	ReceiverSensitiveSubscript receiver_sensitive{5};
	const ReceiverSensitiveSubscript const_receiver_sensitive{7};
	ArgSensitiveSubscript arg_sensitive;

	int index = 8;

	if (receiver_sensitive[0] != 5) {
		return 1;
	}
	if (readConst(const_receiver_sensitive) != 17) {
		return 2;
	}
	if (arg_sensitive[index] != 108) {
		return 3;
	}
	if (arg_sensitive[9] != 209) {
		return 4;
	}

	return 0;
}
