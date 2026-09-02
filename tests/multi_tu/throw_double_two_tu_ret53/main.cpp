#include "shared.h"

double throwFromMain() {
	try {
		throw 2.0;
	} catch (double value) {
		return value;
	}
	return 0.0;
}

int main() {
	return static_cast<int>(throwFromFirst() + throwFromMain() + 50.0);
}
