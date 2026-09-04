#include "shared.h"

void throwIntAcrossSections() {
	throw 17;
}

void throwDoubleAcrossSections() {
	throw 2.5;
}

void throwPayloadAcrossSections() {
	throw ExceptionPayload<int>{123};
}

void throwWidePayloadAcrossSections() {
	throw WidePayload{1234567890123LL, 7};
}

void throwAcrossSections(int kind) {
	if (kind == 0) throwIntAcrossSections();
	if (kind == 1) throwDoubleAcrossSections();
	if (kind == 2) throwPayloadAcrossSections();
	throwWidePayloadAcrossSections();
}
