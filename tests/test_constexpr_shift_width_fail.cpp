// Regression test: invalid constexpr shift counts must be diagnosed using
// the promoted left operand width. `unsigned int` shifts are 32-bit here,
// so shifting by 40 is not a valid constant expression.

static_assert((1u << 40) == 0u);

int main() { return 0; }
