// EH should respect class copy construction both when creating the
// exception object and when binding a catch-by-value parameter.

int g_copy_count = 0;

struct Payload {
	int value;

	Payload(int v) {
		value = v;
	}

	Payload(Payload& other) {
		value = other.value + 10;
		g_copy_count++;
	}
};

int main() {
	Payload p(7);

	try {
		throw p;
	} catch (Payload caught) {
		if (g_copy_count != 2) {
			return g_copy_count + 1;
		}
		return caught.value == 27 ? 0 : caught.value;
	}

	return 99;
}