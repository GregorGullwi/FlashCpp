#include "shared.h"

int firstTranslationUnitValue() {
	return incrementSharedValue(doubleSharedValue(4));
}
