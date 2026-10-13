struct ConstIntReference {
	int value;
	ConstIntReference(const int& argument) noexcept : value(argument) {}
};

struct ConstLongReference {
	long value;
	ConstLongReference(const long& argument) noexcept : value(argument) {}
};

struct ThrowingLongReference {
	ThrowingLongReference(const long&) noexcept(false) {}
};

struct LongRvalueReference {
	long value;
	LongRvalueReference(long&& argument) noexcept : value(argument) {}
};

struct MutableLongReference {
	MutableLongReference(long&) noexcept {}
};

struct RankedScalarReference {
	int selected;
	RankedScalarReference(const int&) noexcept : selected(1) {}
	RankedScalarReference(const long&) noexcept(false) : selected(2) {}
};

template<class T> struct TemplatedLongReference {
	TemplatedLongReference(const long&) noexcept {}
};

template<class T> concept HasNothrowScalarReferenceTemporary = __is_nothrow_constructible(ConstLongReference, T&);
template<class T> requires HasNothrowScalarReferenceTemporary<T> int selectScalar(T&) { return 42; }
int selectScalar(...) { return 1; }

static_assert(__is_constructible(ConstIntReference, short&));
static_assert(__is_constructible(ConstLongReference, short&));
static_assert(__is_constructible(ConstLongReference, const short&));
static_assert(__is_constructible(LongRvalueReference, short&));
static_assert(__is_constructible(LongRvalueReference, const short&));
static_assert(!__is_constructible(MutableLongReference, short&));
static_assert(!__is_constructible(MutableLongReference, const short&));
static_assert(__is_constructible(MutableLongReference, long&));
static_assert(!__is_constructible(MutableLongReference, const long&));
static_assert(!__is_constructible(LongRvalueReference, long&));
static_assert(__is_constructible(LongRvalueReference, long&&));
static_assert(__is_nothrow_constructible(RankedScalarReference, short&));
static_assert(__is_constructible(ThrowingLongReference, short&));
static_assert(!__is_nothrow_constructible(ThrowingLongReference, short&));
static_assert(__is_constructible(TemplatedLongReference<short>, short&));
static_assert(HasNothrowScalarReferenceTemporary<short>);

int main() {
	short argument = 7;
	RankedScalarReference ranked(argument);
	if (ranked.selected != 1) return 1;
	if (selectScalar(argument) != 42) return 4;
	return 0;
}
