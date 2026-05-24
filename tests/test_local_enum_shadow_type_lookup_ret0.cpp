// Regression: local enum type names must resolve to the local enum TypeIndex,
// not an outer enum with the same unqualified name.

enum Mode : long long { GlobalValue = 7 };

int main() {
	enum Mode : unsigned char { LocalValue = 3 };
	Mode local_mode = LocalValue;
	if (sizeof(Mode) != 1) {
		return 1;
	}
	if (static_cast<int>(local_mode) != 3) {
		return 2;
	}
	return 0;
}
