enum FixedByte : unsigned char { fixed_byte_value = 1 };
enum FixedWchar : wchar_t { fixed_wchar_value = 1 };
enum FixedChar32 : char32_t { fixed_char32_value = 1 };

struct ByteChoice { char bytes[1]; };
struct IntChoice { char bytes[2]; };
struct LongChoice { char bytes[3]; };
struct WcharChoice { char bytes[4]; };
struct Char32Choice { char bytes[5]; };
struct UnsignedIntChoice { char bytes[6]; };

ByteChoice choose_underlying(unsigned char);
IntChoice choose_underlying(int);
IntChoice choose_promoted(int);
LongChoice choose_promoted(long);
WcharChoice choose_wchar_preferred(wchar_t);
IntChoice choose_wchar_preferred(int);
IntChoice choose_wchar_rank(int);
LongChoice choose_wchar_rank(long);
Char32Choice choose_char32_preferred(char32_t);
UnsignedIntChoice choose_char32_preferred(unsigned int);
UnsignedIntChoice choose_char32_rank(unsigned int);
LongChoice choose_char32_rank(long);
ByteChoice choose_underlying_ref(const unsigned char&);
IntChoice choose_underlying_ref(const int&);
IntChoice choose_promoted_ref(const int&);
LongChoice choose_promoted_ref(const long&);
WcharChoice choose_wchar_preferred_ref(const wchar_t&);
IntChoice choose_wchar_preferred_ref(const int&);
IntChoice choose_wchar_rank_ref(const int&);
LongChoice choose_wchar_rank_ref(const long&);
Char32Choice choose_char32_preferred_ref(const char32_t&);
UnsignedIntChoice choose_char32_preferred_ref(const unsigned int&);
UnsignedIntChoice choose_char32_rank_ref(const unsigned int&);
LongChoice choose_char32_rank_ref(const long&);

static_assert(sizeof(decltype(choose_underlying(FixedByte::fixed_byte_value))) ==
	sizeof(ByteChoice));
static_assert(sizeof(decltype(choose_underlying_ref(FixedByte::fixed_byte_value))) ==
	sizeof(ByteChoice));
static_assert(sizeof(decltype(choose_promoted(FixedByte::fixed_byte_value))) ==
	sizeof(IntChoice));
static_assert(sizeof(decltype(choose_promoted_ref(FixedByte::fixed_byte_value))) ==
	sizeof(IntChoice));
static_assert(sizeof(decltype(choose_wchar_preferred(FixedWchar::fixed_wchar_value))) ==
	sizeof(WcharChoice));
static_assert(sizeof(decltype(choose_wchar_preferred_ref(FixedWchar::fixed_wchar_value))) ==
	sizeof(WcharChoice));
static_assert(sizeof(decltype(choose_wchar_rank(FixedWchar::fixed_wchar_value))) ==
	sizeof(IntChoice));
static_assert(sizeof(decltype(choose_wchar_rank_ref(FixedWchar::fixed_wchar_value))) ==
	sizeof(IntChoice));
static_assert(sizeof(decltype(choose_char32_preferred(FixedChar32::fixed_char32_value))) ==
	sizeof(Char32Choice));
static_assert(sizeof(decltype(choose_char32_preferred_ref(FixedChar32::fixed_char32_value))) ==
	sizeof(Char32Choice));
static_assert(sizeof(decltype(choose_char32_rank(FixedChar32::fixed_char32_value))) ==
	sizeof(UnsignedIntChoice));
static_assert(sizeof(decltype(choose_char32_rank_ref(FixedChar32::fixed_char32_value))) ==
	sizeof(UnsignedIntChoice));

int main() {
	return 0;
}
