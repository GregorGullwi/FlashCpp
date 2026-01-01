// Test static member referencing another static member in the same struct
struct Test {
    static constexpr bool is_signed = true;
    static constexpr bool is_modulo = !is_signed;  // References is_signed
};

int main() {
    // is_modulo should be false (because is_signed is true)
    return Test::is_modulo ? 0 : 42;
}
