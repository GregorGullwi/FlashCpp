// typeid on a member-function-pointer type-id must resolve to a type_info whose
// canonical mangling carries the owner class, the function signature, and the
// member function's cv/ref/varargs/noexcept qualifiers. The legacy flat
// TypeIndex only projects the pointee category, so codegen previously failed
// with "unable to resolve type_info symbol" for every member-function-pointer
// spelling, including a pointer return type.
//
// The same canonical path keeps data-member pointers and plain function
// pointers distinct from their pointee and from each other at runtime.

struct C {
	int f();
	int* g();
	void h(int) const;
	void i(int) &;
	int value;
};

enum TypeidSlot {
	Mfp = 0,
	MfpAgain,
	MfpVoidInt,
	MfpPointerReturn,
	MfpConstRef,
	MfpVolatile,
	MfpNoexcept,
	MfpVarargs,
	DataMember,
	FunctionPointer,
	ObjectPointer,
	PlainInt,
	SlotCount
};

int main() {
	// Reading the identities through an array keeps the comparisons at runtime
	// instead of letting the compiler fold the typeid operands.
	const void* identities[SlotCount];
	identities[Mfp] = typeid(int (C::*)());
	identities[MfpAgain] = typeid(int (C::*)());
	identities[MfpVoidInt] = typeid(void (C::*)(int));
	identities[MfpPointerReturn] = typeid(int* (C::*)());
	identities[MfpConstRef] = typeid(void (C::*)(int) const &);
	identities[MfpVolatile] = typeid(void (C::*)(int) volatile);
	identities[MfpNoexcept] = typeid(void (C::*)(int) noexcept);
	identities[MfpVarargs] = typeid(void (C::*)(int, ...));
	identities[DataMember] = typeid(int C::*);
	identities[FunctionPointer] = typeid(int (*)(int));
	identities[ObjectPointer] = typeid(int*);
	identities[PlainInt] = typeid(int);

	const void* mfp = identities[Mfp];
	const void* data_member = identities[DataMember];
	const void* function_pointer = identities[FunctionPointer];
	const void* plain_int = identities[PlainInt];

	// The same member-function-pointer type must have one stable identity.
	if (mfp == nullptr || data_member == nullptr || function_pointer == nullptr) {
		return 1;
	}
	if (mfp != identities[MfpAgain]) {
		return 2;
	}

	// Different return types, signatures, and qualifiers are distinct types.
	if (mfp == identities[MfpVoidInt]) {
		return 3;
	}
	if (mfp == identities[MfpPointerReturn]) {
		return 4;
	}
	if (mfp == identities[MfpConstRef]) {
		return 5;
	}
	if (identities[MfpConstRef] == identities[MfpVolatile]) {
		return 6;
	}
	if (mfp == identities[MfpNoexcept]) {
		return 7;
	}
	if (mfp == identities[MfpVarargs]) {
		return 8;
	}

	// Data-member pointers, plain function pointers, and member-function
	// pointers are three ABI-distinct pointer families.
	if (mfp == data_member) {
		return 9;
	}
	if (mfp == function_pointer) {
		return 10;
	}
	if (data_member == function_pointer) {
		return 11;
	}
	// A pointer type is not its pointee.
	if (data_member == plain_int) {
		return 12;
	}
	if (function_pointer == plain_int) {
		return 13;
	}
	if (identities[ObjectPointer] == plain_int) {
		return 14;
	}
	if (identities[ObjectPointer] == function_pointer) {
		return 15;
	}

	// The pointer-return overload and the cv/ref-qualified overload keep their
	// own identities across repeated reads.
	if (identities[MfpPointerReturn] != typeid(int* (C::*)())) {
		return 16;
	}
	if (identities[MfpConstRef] != typeid(void (C::*)(int) const &)) {
		return 17;
	}

	return 0;
}
