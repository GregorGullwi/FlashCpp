#include "shared.h"

int consume_object(ObjectPtr pointer) {
	return pointer == nullptr ? 0 : 1;
}

int consume_function(FunctionPtr pointer) {
	return pointer == nullptr ? 0 : 1;
}

int identity_owner_object(int ObjectOwner::* pointer) {
	return pointer == nullptr ? 0 : 1;
}

int identity_owner_function(int (FunctionOwner::* pointer)()) {
	return pointer == nullptr ? 0 : 1;
}

int consume_specialized_int(SpecializedIntPtr pointer) {
	return pointer == nullptr ? 0 : 1;
}

int consume_specialized_long(SpecializedLongPtr pointer) {
	return pointer == nullptr ? 0 : 1;
}

int consume_specialized_fn_int(SpecializedIntFunctionPtr pointer) {
	return pointer == nullptr ? 0 : 1;
}

int consume_specialized_fn_long(SpecializedLongFunctionPtr pointer) {
	return pointer == nullptr ? 0 : 1;
}
