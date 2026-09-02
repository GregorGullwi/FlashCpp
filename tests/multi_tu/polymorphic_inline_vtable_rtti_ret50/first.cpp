#include "shared.h"

int polymorphicIdentityTag(const PolymorphicBase& object) {
	// The argument is a derived object; dynamic typeid must not collapse to the
	// static PolymorphicBase type when the vtable/RTTI COMDATs are merged.
	return static_cast<int>(typeid(object) != typeid(PolymorphicBase)) + object.tagValue();
}
