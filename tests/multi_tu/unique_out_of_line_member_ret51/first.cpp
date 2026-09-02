#include "shared.h"

UniqueOutOfLine::UniqueOutOfLine() : value_(50), extra_(1) {
}

UniqueOutOfLine::~UniqueOutOfLine() {
}

int UniqueOutOfLine::uniqueValue() const {
	return value_;
}

short UniqueOutOfLine::extra() const {
	return extra_;
}

int uniqueFreeHelper() {
	return 0;
}
