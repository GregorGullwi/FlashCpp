enum UnscopedByte : unsigned char { UnscopedByteValue };
enum class ScopedByte : unsigned char { ScopedByteValue };

struct TakesInt {
	TakesInt(int) noexcept(true) {}
};

struct TakesLong {
	TakesLong(long) noexcept(true) {}
};

struct TakesFloat {
	TakesFloat(float) noexcept(true) {}
};

struct PreferIntegralPromotion {
	PreferIntegralPromotion(int) noexcept(true) {}
	PreferIntegralPromotion(long) noexcept(false) {}
};

static_assert(__is_constructible(int, UnscopedByte));
static_assert(__is_nothrow_constructible(int, UnscopedByte));
static_assert(!__is_constructible(int, ScopedByte));
static_assert(__is_constructible(TakesInt, UnscopedByte));
static_assert(__is_nothrow_constructible(TakesInt, UnscopedByte));
static_assert(__is_constructible(TakesLong, UnscopedByte));
static_assert(__is_constructible(TakesFloat, UnscopedByte));
static_assert(__is_nothrow_constructible(PreferIntegralPromotion, UnscopedByte));
static_assert(!__is_constructible(TakesInt, ScopedByte));

int main() {
	return 0;
}
