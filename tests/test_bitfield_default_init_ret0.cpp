// Test: bitfield with default member initializer
// The parser must stop the bitfield width expression before '='
struct Flags {
    unsigned _M_msb : 1 = 0;
    unsigned _M_lsb : 1 = 0;
    unsigned _M_mid : 4 = 0;
};

int main() {
    Flags f;
    // Default values should all be 0
    if (f._M_msb != 0) return 1;
    if (f._M_lsb != 0) return 2;
    if (f._M_mid != 0) return 3;
    return 0;
}
