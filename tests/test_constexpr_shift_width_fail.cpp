// Regression test: invalid constexpr shift counts must be diagnosed using
// the promoted left operand width. `unsigned int` shifts are 32-bit here,
// so shifting by 40 is not a valid constant expression.

constexpr unsigned int bad_shift = 1u << 40;

int main() { return 0; }
