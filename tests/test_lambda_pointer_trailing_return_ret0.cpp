// A lambda trailing return type must consume pointer and reference
// declarator modifiers before looking for the body.

static int value = 7;

int main() {
	auto get_pointer = []() -> int* { return &value; };
	if (*get_pointer() != 7) {
		return 1;
	}
	auto get_reference = []() -> int& { return value; };
	int& reference = get_reference();
	if (reference != 7) {
		return 2;
	}
	return 0;
}
