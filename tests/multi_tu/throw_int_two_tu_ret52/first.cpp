#include "shared.h"

int throwFromFirst() {
	try {
		throw 1;
	} catch (int value) {
		return value;
	}
	return 0;
}
