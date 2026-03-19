// Test: unsigned integer wrapping at declared type width in constexpr evaluation.
// Per C++20, unsigned arithmetic wraps modulo 2^N where N is the type width.

// unsigned int wraps at 32 bits
constexpr unsigned int sub_wrap = 1u - 2u;
static_assert(sub_wrap == 4294967295u);  // UINT_MAX

// unsigned int overflow at maximum
constexpr unsigned int add_overflow = 4294967295u + 1u;
static_assert(add_overflow == 0u);

// unsigned int multiplication wrap: 100000 * 100000 = 10^10 = 10000000000
// 10000000000 mod 2^32 (4294967296) = 1410065408
constexpr unsigned int mul_wrap = 100000u * 100000u;
static_assert(mul_wrap == 1410065408u);

// unsigned long long arithmetic at full 64-bit width (no masking)
constexpr unsigned long long big = 18000000000000000000ULL + 1ULL;
static_assert(big == 18000000000000000001ULL);

// arithmetic result type propagates correctly through further operations
constexpr unsigned int chain = (1u + 2u) * 3u;
static_assert(chain == 9u);

int main() { return 0; }
