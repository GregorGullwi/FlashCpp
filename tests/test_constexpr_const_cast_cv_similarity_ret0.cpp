// C++20 [expr.const.cast]/3 allows a const_cast between similar object-pointer
// types whose pointer, array, and member-pointer components are identical, so cv
// may differ at every level: the pointee cv (`const int*` to `int*`), an inner
// pointer cv (`long long* const*` to `long long**`), and a deep pointee cv
// (`unsigned short**` to `const unsigned short**`) are all valid. The cast keeps
// the pointed-to object, and a rule that only ignored the outermost cv would
// reject these. [expr.static.cast]/5 covers the other direction, adding cv, and
// must pass the value through instead of converting the pointer to a scalar.

struct CvSimilarityPair {
	int first;
	int second;
	constexpr CvSimilarityPair(int a, int b) : first(a), second(b) {}
};

constexpr int removePointeeConst() {
	int value = 40;
	const int* pointer = &value;
	return *const_cast<int*>(pointer);
}

constexpr int removeInnerPointerConst() {
	long long value = 40;
	long long* pointer = &value;
	long long* const* innerConst = &pointer;
	long long** plain = const_cast<long long**>(innerConst);
	return static_cast<int>(**plain);
}

constexpr int addDeepPointeeConst() {
	unsigned short value = 40;
	unsigned short* pointer = &value;
	unsigned short** plain = &pointer;
	const unsigned short** qualified = const_cast<const unsigned short**>(plain);
	return static_cast<int>(**qualified);
}

constexpr int constCastStructMember() {
	CvSimilarityPair pair(17, 23);
	const CvSimilarityPair* pointer = &pair;
	return const_cast<CvSimilarityPair*>(pointer)->first + pair.second;
}

constexpr int staticAddPointeeConst() {
	int value = 40;
	int* pointer = &value;
	const int* qualified = static_cast<const int*>(pointer);
	return *qualified;
}

constexpr int staticAddNestedConst() {
	int value = 40;
	int* pointer = &value;
	int** plain = &pointer;
	const int* const* qualified = static_cast<const int* const*>(plain);
	return **qualified;
}

constexpr int staticStructPointerPassThrough() {
	CvSimilarityPair pair(17, 23);
	CvSimilarityPair* pointer = &pair;
	const CvSimilarityPair* qualified = static_cast<const CvSimilarityPair*>(pointer);
	return qualified->second;
}

static_assert(removePointeeConst() == 40);
static_assert(removeInnerPointerConst() == 40);
static_assert(addDeepPointeeConst() == 40);
static_assert(constCastStructMember() == 40);
static_assert(staticAddPointeeConst() == 40);
static_assert(staticAddNestedConst() == 40);
static_assert(staticStructPointerPassThrough() == 23);

int main() {
	return removePointeeConst() + removeInnerPointerConst() + addDeepPointeeConst() + constCastStructMember() +
			   staticAddPointeeConst() + staticAddNestedConst() + staticStructPointerPassThrough() -
		   263;
}
