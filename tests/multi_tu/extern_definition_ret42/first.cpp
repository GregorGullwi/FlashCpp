#include "shared.h"

// Including the declarations before these definitions must preserve one object.
int sharedValue = 17;
double sharedScale = 2.5;

int updateSharedValues() {
	sharedValue += 25;
	sharedScale += 1.0;
	return sharedValue;
}
