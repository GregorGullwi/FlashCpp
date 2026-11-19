#include "shared.h"

// Conversion-template substitution copies an ordered member-pointer spine whose
// declaring class may be present only as component EntityId/TypeId. Instantiating
// these operators exercises that path. The linked free-function symbols below must
// match MSVC's decorated forms for ordinary and specialized owners.
//
// Conversion-operator template symbols themselves (`??$?B...`) are not asserted in
// expect_strong_defs.txt: FlashCpp still uses a distinct operator-name encoding, while
// the member-pointer owner type codes inside those symbols are covered by the
// free-function fixtures.

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

struct SpecializedIntSelection {
	char value;
};

struct SpecializedLongSelection {
	char value;
};

struct EllipsisSelection {
	long long value;
};

ObjectSelection select_object(ObjectPtr);
EllipsisSelection select_object(...);
FunctionSelection select_function(FunctionPtr);
EllipsisSelection select_function(...);
SpecializedIntSelection select_specialized_int(SpecializedIntPtr);
EllipsisSelection select_specialized_int(...);
SpecializedLongSelection select_specialized_long(SpecializedLongPtr);
EllipsisSelection select_specialized_long(...);

int main() {
	TemplateSource source;
	ObjectPtr object_pointer = source;
	FunctionPtr function_pointer = source;
	SpecializedIntPtr specialized_int = source;
	SpecializedLongPtr specialized_long = source;
	if (object_pointer != nullptr || function_pointer != nullptr ||
		specialized_int != nullptr || specialized_long != nullptr) {
		return 1;
	}
	if (sizeof(select_object(source)) != sizeof(ObjectSelection)) {
		return 2;
	}
	if (sizeof(select_function(source)) != sizeof(FunctionSelection)) {
		return 3;
	}
	if (sizeof(select_specialized_int(source)) != sizeof(SpecializedIntSelection)) {
		return 4;
	}
	if (sizeof(select_specialized_long(source)) != sizeof(SpecializedLongSelection)) {
		return 5;
	}
	return consume_object(object_pointer) + consume_function(function_pointer)
		+ identity_owner_object(object_pointer) + identity_owner_function(function_pointer)
		+ consume_specialized_int(specialized_int) + consume_specialized_long(specialized_long)
		+ consume_specialized_fn_int(static_cast<SpecializedIntFunctionPtr>(nullptr))
		+ consume_specialized_fn_long(static_cast<SpecializedLongFunctionPtr>(nullptr));
}
