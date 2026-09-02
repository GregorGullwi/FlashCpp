#include "shared.h"

double throwFromFirst() {
	try {
		throw 1.0;
	} catch (double value) {
		return value;
	}
	return 0.0;
}
