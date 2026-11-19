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

static_assert(sizeof(AbiSmallPair) == 8);
static_assert(sizeof(AbiSmallDtor) == 4);
static_assert(sizeof(AbiDefaultedCopy) == 4);

extern "C" AbiSmallPair flashcpp_make_small_pair(int first, int second);
extern "C" AbiSmallDtor flashcpp_make_small_dtor(int value);
extern "C" AbiDefaultedCopy flashcpp_make_defaulted_copy(int value);

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
