// Test: shift-count validation for arithmetic-produced left operands.
// (1u + 1u) has type unsigned int (32 bits); shift count 40 >= 32 must be rejected.
static_assert((1u + 1u) << 40);

int main() { return 0; }
