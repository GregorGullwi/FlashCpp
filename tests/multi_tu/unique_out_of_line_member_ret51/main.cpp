#include "shared.h"

int main() {
	UniqueOutOfLine object;
	return object.uniqueValue() + object.extra() + uniqueFreeHelper();
}
