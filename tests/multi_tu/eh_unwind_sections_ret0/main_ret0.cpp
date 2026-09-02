#include "shared.h"

// Nonempty data before the indirect typeinfo slots also checks their offsets.
double exceptionScale = 2.5;

int catchIntAcrossSections() {
	try {
		relayAcrossSections(0);
	} catch (int value) {
		return value == 17 ? 0 : 1;
	}
	return 1;
}

int catchDoubleAcrossSections() {
	try {
		relayAcrossSections(1);
	} catch (double value) {
		return value == exceptionScale ? 0 : 2;
	}
	return 2;
}

int catchPayloadAcrossSections() {
	try {
		relayAcrossSections(2);
	} catch (ExceptionPayload<int>&) {
		// This case checks typed class matching and cross-TU unwind lookup.
		return 0;
	}
	return 4;
}

int main() {
	if (catchIntAcrossSections() != 0) return 1;
	if (catchDoubleAcrossSections() != 0) return 2;
	if (catchPayloadAcrossSections() != 0) return 4;
	return 0;
}
