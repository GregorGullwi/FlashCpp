#include "shared.h"

int main() {
	rtti::MemberFunction const function = nullptr;
	if (rtti::memberFunctionInfo() != typeid(function)) {
		return 1;
	}
	if (rtti::memberFunctionInfo() == typeid(rtti::Value* (rtti::OwnerType::*)(rtti::Value*, rtti::Value*, const rtti::Value&) const &)) {
		return 2;
	}
	if (rtti::memberObjectInfo() != typeid(const int rtti::OwnerType::*)) {
		return 3;
	}
	if (rtti::memberObjectInfo() == typeid(int rtti::OwnerType::*)) {
		return 4;
	}
	if (rtti::nestedMemberInfo() != typeid(rtti::NestedMember) || rtti::nestedMemberInfo() == typeid(int rtti::OwnerType::*)) {
		return 5;
	}
	if (rtti::variadicInfo() != typeid(rtti::Variadic) || rtti::variadicInfo() == typeid(void (rtti::OwnerType::*)(int))) {
		return 6;
	}
	if (rtti::memberArrayInfo() != typeid(rtti::MemberArray) || rtti::memberArrayInfo() == typeid(const int (rtti::OwnerType::*)[4])) {
		return 7;
	}
	return 0;
}
