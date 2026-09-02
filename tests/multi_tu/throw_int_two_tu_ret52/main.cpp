#include "shared.h"

int throwFromMain() {
	try {
		throw 2;
	} catch (int value) {
		return value;
	}
	return 0;
}

int main() {
	return throwFromFirst() + throwFromMain() + 49;
}
