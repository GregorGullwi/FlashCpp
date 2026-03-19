// ~0u: bitwise NOT of unsigned int 0 should be 0xFFFFFFFF (32 bits), not 64-bit max
constexpr unsigned int a = ~0u;
static_assert(a == 4294967295u);

// ~(unsigned int)0 same test via cast
constexpr unsigned int b = ~(unsigned int)0;
static_assert(b == 4294967295u);

// ~0u should NOT be 18446744073709551615ULL (64-bit)
static_assert(a != 18446744073709551615ULL);

// -(1u): unary minus on unsigned int wraps at 32 bits
constexpr unsigned int c = -(1u);
static_assert(c == 4294967295u);

int main() { return 0; }
