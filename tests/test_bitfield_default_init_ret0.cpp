// Test: bitfield with default member initializer
// Verifies parsing of bitfield width before '=' and correct codegen for packed defaults.
struct Flags {
    unsigned _M_msb : 1 = 0;
    unsigned _M_lsb : 1 = 1;
    unsigned _M_mid : 4 = 5;
};

int main() {
    Flags f;
    if (f._M_msb != 0) return 1;
    if (f._M_lsb != 1) return 2;
    if (f._M_mid != 5) return 3;
    return 0;
}
