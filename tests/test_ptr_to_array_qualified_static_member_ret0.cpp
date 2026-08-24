// Regression: an out-of-line static data-member definition may use a
// parenthesized pointer-to-array declarator (C++20 [dcl.meaning]/1).

struct Registry {
	static int (*table)[2][4];
	static const long (*empty)[3];
};

int rows[2][4] = {{10, 11, 12, 13}, {20, 21, 22, 23}};

int (*Registry::table)[2][4] = &rows;
const long (*Registry::empty)[3] = nullptr;

int main() {
	if (sizeof(Registry::table) != sizeof(int (*)[2][4])) {
		return 1;
	}
	if (Registry::table != &rows) {
		return 2;
	}
	if (Registry::empty != nullptr) {
		return 3;
	}
	return 0;
}
