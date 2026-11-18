// Address-of must resolve a by-reference capture through the pointer stored in
// the closure, for both explicit and capture-all forms.

int main() {
	int value = 7;
	auto explicit_capture = [&value]() { return &value; };
	int* explicit_address = explicit_capture();
	if (explicit_address != &value) {
		return 1;
	}
	*explicit_address = 11;
	if (value != 11) {
		return 2;
	}
	auto capture_all = [&]() { return &value; };
	int* all_address = capture_all();
	if (all_address != &value) {
		return 3;
	}
	*all_address = 13;
	return value == 13 ? 0 : 4;
}
