#include "shared.h"

int main() {
	if (sharedValue != 17 || sharedScale != 2.5) return 1;
	if (updateSharedValues() != 42) return 2;
	if (sharedValue != 42 || sharedScale != 3.5) return 3;
	return sharedValue;
}
