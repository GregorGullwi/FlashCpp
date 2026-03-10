// EH should use the move constructor when throwing an xvalue class object,
// while catch-by-value still copy-constructs the catch parameter.

int g_copy_count = 0;
int g_move_count = 0;

struct Payload {
	int value;

	Payload(int v) {
		value = v;
	}

	Payload(Payload& other) {
		value = other.value + 100;
		g_copy_count++;
	}

	Payload(Payload&& other) {
		value = other.value + 10;
		g_move_count++;
	}
};

Payload&& asRValue(Payload& value) {
	return static_cast<Payload&&>(value);
}

int main() {
	Payload payload(7);

	try {
		throw asRValue(payload);
	} catch (Payload caught) {
		if (g_move_count != 1) return 1;
		if (g_copy_count != 1) return 2;
		return caught.value == 117 ? 0 : caught.value;
	}

	return 99;
}