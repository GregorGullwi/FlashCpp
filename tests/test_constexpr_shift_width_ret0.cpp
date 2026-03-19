// Regression test: constexpr shift-count validation should use the
// promoted left operand width, not the evaluator's 64-bit storage width.

constexpr int promoted_from_ushort = static_cast<unsigned short>(1) << 17;
static_assert(promoted_from_ushort == 131072);

constexpr int promoted_from_uchar = static_cast<unsigned char>(1) << 9;
static_assert(promoted_from_uchar == 512);

int main() { return 0; }
