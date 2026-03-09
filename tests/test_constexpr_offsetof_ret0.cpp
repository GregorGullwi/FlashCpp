// Test constexpr offsetof support

#pragma pack(1)
struct PackedLayout {
	char c;
	int i;
	short s;
};
#pragma pack()

struct DefaultLayout {
	char c;
	int i;
	short s;
};

constexpr unsigned long long packed_i = offsetof(PackedLayout, i);
constexpr unsigned long long packed_s = offsetof(PackedLayout, s);
constexpr unsigned long long default_i = offsetof(DefaultLayout, i);
constexpr unsigned long long default_s = offsetof(DefaultLayout, s);

static_assert(offsetof(PackedLayout, c) == 0, "PackedLayout::c offset should be 0");
static_assert(packed_i == 1, "PackedLayout::i offset should be 1");
static_assert(packed_s == 5, "PackedLayout::s offset should be 5");
static_assert(offsetof(DefaultLayout, c) == 0, "DefaultLayout::c offset should be 0");
static_assert(default_i == 4, "DefaultLayout::i offset should be 4");
static_assert(default_s == 8, "DefaultLayout::s offset should be 8");

int main() {
	return static_cast<int>(packed_s + default_i - 9);
}