#include "shared.h"

namespace rtti {
const void* memberFunctionInfo() {
	return typeid(MemberFunction);
}

const void* memberObjectInfo() {
	return typeid(MemberObject);
}

const void* nestedMemberInfo() {
	return typeid(NestedMember);
}

const void* variadicInfo() {
	return typeid(Variadic);
}

const void* memberArrayInfo() {
	return typeid(MemberArray);
}
}
