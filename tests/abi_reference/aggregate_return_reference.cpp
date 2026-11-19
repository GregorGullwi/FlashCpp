// Separately compiled reference object for test_aggregate_return_abi_ret0.cpp.
//
// On Windows this file is built by cl.exe (MSVC x64 ABI); on Linux it is built
// by clang++ (SysV AMD64 ABI). The record definitions must stay identical to
// the test translation unit; the out-of-line `= default` copy constructor is
// user-provided, which is what the ABI checks exercise.

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

AbiDefaultedCopy::AbiDefaultedCopy(const AbiDefaultedCopy&) = default;

struct AbiInnerDefaultCtor {
	int value;
	AbiInnerDefaultCtor() : value(0) {}
};

struct AbiOuterDefaultCtor {
	AbiInnerDefaultCtor inner;
};

struct AbiConstMember {
	const int value;
};

struct AbiInnerDefaultedCopy {
	int value;
	AbiInnerDefaultedCopy() = default;
	AbiInnerDefaultedCopy(const AbiInnerDefaultedCopy&);
};

AbiInnerDefaultedCopy::AbiInnerDefaultedCopy(const AbiInnerDefaultedCopy&) = default;

struct AbiOuterDefaultedCopyMember {
	AbiInnerDefaultedCopy inner;
};

#if !defined(_WIN32)
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

extern "C" AbiSmallPair flashcpp_make_small_pair(int first, int second);
extern "C" AbiSmallDtor flashcpp_make_small_dtor(int value);
extern "C" AbiDefaultedCopy flashcpp_make_defaulted_copy(int value);
extern "C" AbiOuterDefaultCtor flashcpp_make_outer_default_ctor(int value);
extern "C" AbiConstMember flashcpp_make_const_member(int value);
extern "C" AbiOuterDefaultedCopyMember flashcpp_make_outer_defaulted_copy_member(int value);
#if !defined(_WIN32)
extern "C" AbiOverloadedCopy flashcpp_make_overloaded_copy(int value);
#endif

extern "C" AbiSmallPair ref_make_small_pair(int first, int second) {
	AbiSmallPair result;
	result.first = first;
	result.second = second;
	return result;
}

extern "C" AbiSmallDtor ref_make_small_dtor(int value) {
	AbiSmallDtor result;
	result.value = value;
	return result;
}

extern "C" AbiDefaultedCopy ref_make_defaulted_copy(int value) {
	AbiDefaultedCopy result;
	result.value = value;
	return result;
}

extern "C" AbiOuterDefaultCtor ref_make_outer_default_ctor(int value) {
	AbiOuterDefaultCtor result;
	result.inner.value = value;
	return result;
}

extern "C" AbiConstMember ref_make_const_member(int value) {
	AbiConstMember result = {value};
	return result;
}

extern "C" AbiOuterDefaultedCopyMember ref_make_outer_defaulted_copy_member(int value) {
	AbiOuterDefaultedCopyMember result;
	result.inner.value = value;
	return result;
}

#if !defined(_WIN32)
extern "C" AbiOverloadedCopy ref_make_overloaded_copy(int value) {
	AbiOverloadedCopy result;
	result.value = value;
	return result;
}
#endif

extern "C" int ref_call_flashcpp_make_small_pair(int first, int second) {
	AbiSmallPair result = flashcpp_make_small_pair(first, second);
	return result.first + result.second;
}

extern "C" int ref_call_flashcpp_make_small_dtor(int value) {
	AbiSmallDtor result = flashcpp_make_small_dtor(value);
	return result.value;
}

extern "C" int ref_call_flashcpp_make_defaulted_copy(int value) {
	AbiDefaultedCopy result = flashcpp_make_defaulted_copy(value);
	return result.value;
}

extern "C" int ref_call_flashcpp_make_outer_default_ctor(int value) {
	AbiOuterDefaultCtor result = flashcpp_make_outer_default_ctor(value);
	return result.inner.value;
}

extern "C" int ref_call_flashcpp_make_const_member(int value) {
	AbiConstMember result = flashcpp_make_const_member(value);
	return result.value;
}

extern "C" int ref_call_flashcpp_make_outer_defaulted_copy_member(int value) {
	AbiOuterDefaultedCopyMember result = flashcpp_make_outer_defaulted_copy_member(value);
	return result.inner.value;
}

#if !defined(_WIN32)
extern "C" int ref_call_flashcpp_make_overloaded_copy(int value) {
	AbiOverloadedCopy result = flashcpp_make_overloaded_copy(value);
	return result.value;
}
#endif
