// Test: Bitfield write uses sign-extended imm8/imm32 AND encoding for inverted masks.
// A 3-bit field at offset 0 generates clear_mask = ~(0x7 << 0) = 0xFFFFFFFFFFFFFFF8,
// which should use AND r/m64, imm8 (sign-extended 0xF8) rather than the full 64-bit path.
struct S {
    unsigned x : 3;
    unsigned y : 5;
};

int main() {
    S s = {};
    s.x = 2;
    s.y = 5;
    return s.x + s.y * 8; // 2 + 5*8 = 42
}
