// Cross-compiler aggregate return ABI checks.
//
// Win64 (MSVC x64 ABI, reference object built by cl.exe): a record returns in
// RAX only for 1/2/4/8-byte POD-like records. A 4-byte record with a
// user-provided destructor, and a record whose copy constructor is defaulted
// out of line (user-provided per [dcl.fct.def.default]/5), use the caller's
// hidden return slot instead.
//
// SysV AMD64 (reference object built by clang++ on Linux): the same non-trivial
// cases are indirect because the copy/move construction or destruction is
// non-trivial for the purposes of calls, while the trivial pair returns in
// registers. The reference translation unit is compiled separately by the host
// C++ compiler, so the caller side and the callee side are both checked against
// a toolchain that does not share FlashCpp's classifier.

struct AbiSmallPair {
	int first;
	int second;
};

struct AbiSmallDtor {
	int value;
	~AbiSmallDtor() {}
};

struct AbiDefaultedCopy {
	int value;
	AbiDefaultedCopy() = default;
	AbiDefaultedCopy(const AbiDefaultedCopy&);
};

struct AbiInnerDefaultCtor {
	int value;
	AbiInnerDefaultCtor() : value(0) {}
};

// Win64 checks the root record's constructors but only the member subobjects'
// copy assignment and destruction. A member with a user-provided default
// constructor therefore does not force this record into the hidden slot.
struct AbiOuterDefaultCtor {
	AbiInnerDefaultCtor inner;
};

// A const data member deletes the implicit copy assignment, which makes the
// record indirect for the Win64 return ABI even though it is four bytes.
struct AbiConstMember {
	const int value;
};

// Win64 ignores the member's non-trivial copy constructor here and still
// returns the enclosing record in EAX. SysV does not: the out-of-line
// defaulted copy constructor is user-provided, so the enclosing record becomes
// non-trivial for calls.
struct AbiInnerDefaultedCopy {
	int value;
	AbiInnerDefaultedCopy() = default;
	AbiInnerDefaultedCopy(const AbiInnerDefaultedCopy&);
};

struct AbiOuterDefaultedCopyMember {
	AbiInnerDefaultedCopy inner;
};

#if !defined(_WIN32)
// MSVC rejects two copy-constructor overloads when one is defaulted, so this
// SysV-only case exercises the all-overload scan. Clang uses a hidden slot
// because the non-const overload is user-provided.
struct AbiOverloadedCopy {
	int value;
	AbiOverloadedCopy() = default;
	AbiOverloadedCopy(const AbiOverloadedCopy&) = default;
	AbiOverloadedCopy(AbiOverloadedCopy&) {
		value = 0;
	}
};
#endif

static_assert(sizeof(AbiSmallPair) == 8);
static_assert(sizeof(AbiSmallDtor) == 4);
static_assert(sizeof(AbiDefaultedCopy) == 4);
static_assert(sizeof(AbiOuterDefaultCtor) == 4);
static_assert(sizeof(AbiConstMember) == 4);
static_assert(sizeof(AbiOuterDefaultedCopyMember) == 4);

extern "C" AbiSmallPair ref_make_small_pair(int first, int second);
extern "C" AbiSmallDtor ref_make_small_dtor(int value);
extern "C" AbiDefaultedCopy ref_make_defaulted_copy(int value);
extern "C" AbiOuterDefaultCtor ref_make_outer_default_ctor(int value);
extern "C" AbiConstMember ref_make_const_member(int value);
extern "C" AbiOuterDefaultedCopyMember ref_make_outer_defaulted_copy_member(int value);
extern "C" int ref_call_flashcpp_make_small_pair(int first, int second);
extern "C" int ref_call_flashcpp_make_small_dtor(int value);
extern "C" int ref_call_flashcpp_make_defaulted_copy(int value);
extern "C" int ref_call_flashcpp_make_outer_default_ctor(int value);
extern "C" int ref_call_flashcpp_make_const_member(int value);
extern "C" int ref_call_flashcpp_make_outer_defaulted_copy_member(int value);
#if !defined(_WIN32)
extern "C" AbiOverloadedCopy ref_make_overloaded_copy(int value);
extern "C" int ref_call_flashcpp_make_overloaded_copy(int value);
#endif

extern "C" AbiSmallPair flashcpp_make_small_pair(int first, int second) {
	AbiSmallPair result;
	result.first = first;
	result.second = second;
	return result;
}

extern "C" AbiSmallDtor flashcpp_make_small_dtor(int value) {
	AbiSmallDtor result;
	result.value = value;
	return result;
}

extern "C" AbiDefaultedCopy flashcpp_make_defaulted_copy(int value) {
	AbiDefaultedCopy result;
	result.value = value;
	return result;
}

extern "C" AbiOuterDefaultCtor flashcpp_make_outer_default_ctor(int value) {
	AbiOuterDefaultCtor result;
	result.inner.value = value;
	return result;
}

extern "C" AbiConstMember flashcpp_make_const_member(int value) {
	AbiConstMember result = {value};
	return result;
}

extern "C" AbiOuterDefaultedCopyMember flashcpp_make_outer_defaulted_copy_member(int value) {
	AbiOuterDefaultedCopyMember result;
	result.inner.value = value;
	return result;
}

#if !defined(_WIN32)
extern "C" AbiOverloadedCopy flashcpp_make_overloaded_copy(int value) {
	AbiOverloadedCopy result;
	result.value = value;
	return result;
}
#endif

extern "C" int main() {
	AbiSmallPair pair = ref_make_small_pair(20, 22);
	if (pair.first != 20 || pair.second != 22) {
		return 1;
	}

	AbiSmallDtor dtor = ref_make_small_dtor(42);
	if (dtor.value != 42) {
		return 2;
	}

	AbiDefaultedCopy copy = ref_make_defaulted_copy(43);
	if (copy.value != 43) {
		return 3;
	}

	AbiOuterDefaultCtor outer = ref_make_outer_default_ctor(44);
	if (outer.inner.value != 44) {
		return 4;
	}

	AbiConstMember const_member = ref_make_const_member(45);
	if (const_member.value != 45) {
		return 5;
	}

	AbiOuterDefaultedCopyMember outer_defaulted_copy = ref_make_outer_defaulted_copy_member(46);
	if (outer_defaulted_copy.inner.value != 46) {
		return 6;
	}

	if (ref_call_flashcpp_make_small_pair(4, 5) != 9) {
		return 7;
	}
	if (ref_call_flashcpp_make_small_dtor(47) != 47) {
		return 8;
	}
	if (ref_call_flashcpp_make_defaulted_copy(48) != 48) {
		return 9;
	}
	if (ref_call_flashcpp_make_outer_default_ctor(49) != 49) {
		return 10;
	}
	if (ref_call_flashcpp_make_const_member(50) != 50) {
		return 11;
	}
	if (ref_call_flashcpp_make_outer_defaulted_copy_member(51) != 51) {
		return 12;
	}

#if !defined(_WIN32)
	AbiOverloadedCopy overloaded = ref_make_overloaded_copy(52);
	if (overloaded.value != 52) {
		return 13;
	}
	if (ref_call_flashcpp_make_overloaded_copy(53) != 53) {
		return 14;
	}
#endif
	return 0;
}
