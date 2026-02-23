// Test: bitfield with default member initializer
// The parser must stop the bitfield width expression before '='
// Tests both zero and non-zero default values to verify parsing.
struct Flags {
    unsigned _M_msb : 1 = 0;
    unsigned _M_lsb : 1 = 1;
    unsigned _M_mid : 4 = 5;
};

int main() {
    // Verify the struct with bitfield default initializers parses correctly.
    // Runtime bitfield default initialization is not yet fully implemented in codegen.
    Flags f;
    (void)f;
    return 0;
}
