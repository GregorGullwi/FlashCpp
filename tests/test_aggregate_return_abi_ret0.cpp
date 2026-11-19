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

static_assert(sizeof(AbiSmallPair) == 8);
static_assert(sizeof(AbiSmallDtor) == 4);
static_assert(sizeof(AbiDefaultedCopy) == 4);

extern "C" AbiSmallPair ref_make_small_pair(int first, int second);
extern "C" AbiSmallDtor ref_make_small_dtor(int value);
extern "C" AbiDefaultedCopy ref_make_defaulted_copy(int value);
extern "C" int ref_call_flashcpp_make_small_pair(int first, int second);
extern "C" int ref_call_flashcpp_make_small_dtor(int value);
extern "C" int ref_call_flashcpp_make_defaulted_copy(int value);

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

	if (ref_call_flashcpp_make_small_pair(4, 5) != 9) {
		return 4;
	}
	if (ref_call_flashcpp_make_small_dtor(44) != 44) {
		return 5;
	}
	if (ref_call_flashcpp_make_defaulted_copy(45) != 45) {
		return 6;
	}
	return 0;
}
