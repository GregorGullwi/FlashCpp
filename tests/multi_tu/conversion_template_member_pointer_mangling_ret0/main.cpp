#include "shared.h"

// Conversion-template substitution copies an ordered member-pointer spine whose
// declaring class may be present only as component EntityId/TypeId. Instantiating
// these operators exercises that path, and the linked consume_* symbols must match
// MSVC's decorated forms for the two distinct owners.

struct TemplateSource {
	template<class T>
	operator T() const {
		return T();
	}
};

struct ObjectSelection {
	char value;
};

struct FunctionSelection {
	char value;
};

struct EllipsisSelection {
	long long value;
};

ObjectSelection select_object(ObjectPtr);
EllipsisSelection select_object(...);
FunctionSelection select_function(FunctionPtr);
EllipsisSelection select_function(...);

int main() {
	TemplateSource source;
	ObjectPtr object_pointer = source;
	FunctionPtr function_pointer = source;
	if (object_pointer != nullptr || function_pointer != nullptr) {
		return 1;
	}
	if (sizeof(select_object(source)) != sizeof(ObjectSelection)) {
		return 2;
	}
	if (sizeof(select_function(source)) != sizeof(FunctionSelection)) {
		return 3;
	}
	return consume_object(object_pointer) + consume_function(function_pointer)
		+ identity_owner_object(object_pointer) + identity_owner_function(function_pointer);
}
