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
