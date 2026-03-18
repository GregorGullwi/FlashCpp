// Test nested constexpr offsetof support

#pragma pack(1)
struct PackedInner {
	char c;
	int value;
};

struct PackedOuter {
	char pad;
	PackedInner inner;
};
#pragma pack()

struct DefaultInner {
	char c;
	int value;
};

struct DefaultOuter {
	char pad;
	DefaultInner inner;
};

constexpr unsigned long long packed_value_offset = offsetof(PackedOuter, inner.value);
constexpr unsigned long long default_value_offset = offsetof(DefaultOuter, inner.value);

static_assert(offsetof(PackedOuter, inner.c) == 1, "PackedOuter::inner.c offset should be 1");
static_assert(packed_value_offset == 2, "PackedOuter::inner.value offset should be 2");
static_assert(offsetof(DefaultOuter, inner.c) == 4, "DefaultOuter::inner.c offset should be 4");
static_assert(default_value_offset == 8, "DefaultOuter::inner.value offset should be 8");

int main() {
	return static_cast<int>(packed_value_offset + default_value_offset - 10);
}
