#include "shared.h"

int firstTranslationUnitValue() {
	PolymorphicTemplate<int> object;
	return polymorphicTemplateIdentityTag(object, 1);
}
