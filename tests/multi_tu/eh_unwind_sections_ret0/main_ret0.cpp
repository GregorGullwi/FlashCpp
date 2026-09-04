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
	} catch (ExceptionPayload<int>& payload) {
		return payload.value == 123 ? 0 : 4;
	}
	return 4;
}

int catchWidePayloadAcrossSections() {
	try {
		relayAcrossSections(3);
	} catch (WidePayload& payload) {
		if (payload.high != 1234567890123LL) return 5;
		if (payload.low != 7) return 6;
		return 0;
	}
	return 5;
}

int main() {
	if (catchIntAcrossSections() != 0) return 1;
	if (catchDoubleAcrossSections() != 0) return 2;
	if (catchPayloadAcrossSections() != 0) return 4;
	if (catchWidePayloadAcrossSections() != 0) return catchWidePayloadAcrossSections();
	return 0;
}
