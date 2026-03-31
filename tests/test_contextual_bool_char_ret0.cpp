// Contextual bool conversion: char → bool in control-flow conditions
// C++20 [conv.bool]: zero value → false, nonzero → true

int main() {
	char c = 'A';
	char zero = '\0';

 // if: nonzero char is true
	if (c) {
	// ok
	} else {
		return 1;
	}

 // if: null char is false
	if (zero) {
		return 2;
	}

 // ternary with char condition
	int result = c ? 10 : 20;
	if (result != 10) {
		return 3;
	}
	int result2 = zero ? 10 : 20;
	if (result2 != 20) {
		return 4;
	}

	return 0;
}
