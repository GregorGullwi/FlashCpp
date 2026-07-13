enum class ByteTag : unsigned char {
	Value = 1
};

struct Payload {
	long long wide;
	short narrow;
};

using PayloadAlias = Payload;
using ScalarAlias = long long;
using EnumAlias = ByteTag;

struct Aggregate {
	PayloadAlias nested;
	PayloadAlias grid[2][3];
};

static_assert(sizeof(PayloadAlias) == sizeof(Payload));
static_assert(sizeof(ScalarAlias) == sizeof(long long));
static_assert(sizeof(EnumAlias) == sizeof(unsigned char));

int main() {
	Aggregate value{};

	return sizeof(value.nested) == sizeof(Payload) &&
			sizeof(value.grid) == sizeof(Payload) * 6 &&
			sizeof(value.grid[0]) == sizeof(Payload) * 3 &&
			sizeof(value.grid[0][0]) == sizeof(Payload)
		? 0
		: 1;
}
