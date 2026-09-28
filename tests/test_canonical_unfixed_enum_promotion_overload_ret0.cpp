enum SmallUnfixed { small_value = 1 };
enum UnsignedIntRange { unsigned_int_range_value = 2147483648LL };
enum SignedWideRange {
	negative_wide_value = -2147483649LL,
	positive_wide_value = 2147483648LL,
};

struct IntChoice { char bytes[1]; };
struct UnsignedIntChoice { char bytes[2]; };
struct LongChoice { char bytes[3]; };
struct LongLongChoice { char bytes[4]; };

IntChoice choose_small(int);
UnsignedIntChoice choose_small(unsigned int);
IntChoice choose_small_ref(const int&);
UnsignedIntChoice choose_small_ref(const unsigned int&);
UnsignedIntChoice choose_unsigned_range(unsigned int);
LongChoice choose_unsigned_range(long);
UnsignedIntChoice choose_unsigned_range_ref(const unsigned int&);
LongChoice choose_unsigned_range_ref(const long&);
LongChoice choose_signed_wide(long);
LongLongChoice choose_signed_wide(long long);
LongChoice choose_signed_wide_ref(const long&);
LongLongChoice choose_signed_wide_ref(const long long&);

static_assert(sizeof(decltype(choose_small(SmallUnfixed::small_value))) ==
	sizeof(IntChoice));
static_assert(sizeof(decltype(choose_small_ref(SmallUnfixed::small_value))) ==
	sizeof(IntChoice));
static_assert(sizeof(decltype(choose_unsigned_range(
	UnsignedIntRange::unsigned_int_range_value))) == sizeof(UnsignedIntChoice));
static_assert(sizeof(decltype(choose_unsigned_range_ref(
	UnsignedIntRange::unsigned_int_range_value))) == sizeof(UnsignedIntChoice));
static_assert(sizeof(decltype(choose_signed_wide(
	SignedWideRange::positive_wide_value))) ==
	(sizeof(long) == sizeof(long long)
		? sizeof(LongChoice)
		: sizeof(LongLongChoice)));
static_assert(sizeof(decltype(choose_signed_wide_ref(
	SignedWideRange::positive_wide_value))) ==
	(sizeof(long) == sizeof(long long)
		? sizeof(LongChoice)
		: sizeof(LongLongChoice)));
static_assert(sizeof(UnsignedIntRange) == sizeof(unsigned int));
static_assert(sizeof(SignedWideRange) ==
	(sizeof(long) == sizeof(long long) ? sizeof(long) : sizeof(long long)));

int main() {
	return 0;
}
