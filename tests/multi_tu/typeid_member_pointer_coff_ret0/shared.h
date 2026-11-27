#pragma once

namespace rtti {
struct Value {};
template<typename T> struct Owner {
	struct Inner {};
};

using OwnerType = Owner<int>;
using MemberFunction = Value* (OwnerType::*)(Value*, Value*, const Value&) const & noexcept;
using Variadic = void (OwnerType::*)(int, ...);
using MemberObject = const int OwnerType::*;
using InnerType = OwnerType::Inner;
using NestedMember = int InnerType::*;
using MemberArray = const int (OwnerType::*)[3];

const void* memberFunctionInfo();
const void* memberObjectInfo();
const void* nestedMemberInfo();
const void* variadicInfo();
const void* memberArrayInfo();
}
