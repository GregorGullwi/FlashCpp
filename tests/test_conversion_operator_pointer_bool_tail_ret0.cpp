// A pointer-valued conversion function can be followed by the standard
// pointer-to-bool conversion during overload resolution.
int conversion_target = 1;

struct PointerSource {
	operator int*() const { return &conversion_target; }
};

int select_conversion(bool) { return 23; }
int select_conversion(...) { return 31; }

int main() {
	PointerSource source;
	const bool initialized = source;
	if (!initialized) {
		return 1;
	}
	return select_conversion(source) == 23 ? 0 : 2;
}
