// Comprehensive constexpr test - Working Features Only

// ===== BASIC CONSTEXPR VARIABLES =====
constexpr int literal_int = 42;
static_assert(literal_int == 42, "literal_int should be 42");

constexpr double literal_double = 3.14;

constexpr bool literal_bool = true;
static_assert(literal_bool, "literal_bool should be true");

// ===== ARITHMETIC OPERATIONS =====
constexpr int add_result = 10 + 20;
static_assert(add_result == 30, "10 + 20 should be 30");

constexpr int sub_result = 50 - 15;
static_assert(sub_result == 35, "50 - 15 should be 35");

constexpr int mul_result = 7 * 6;
static_assert(mul_result == 42, "7 * 6 should be 42");

constexpr int div_result = 100 / 5;
static_assert(div_result == 20, "100 / 5 should be 20");

constexpr int mod_result = 17 % 5;
static_assert(mod_result == 2, "17 % 5 should be 2");

// ===== BITWISE OPERATIONS =====
constexpr int bitwise_and = 12 & 10;
static_assert(bitwise_and == 8, "12 & 10 should be 8");

constexpr int bitwise_or = 12 | 10;
static_assert(bitwise_or == 14, "12 | 10 should be 14");

constexpr int bitwise_xor = 12 ^ 10;
static_assert(bitwise_xor == 6, "12 ^ 10 should be 6");

constexpr int left_shift = 1 << 4;
static_assert(left_shift == 16, "1 << 4 should be 16");

constexpr int right_shift = 32 >> 2;
static_assert(right_shift == 8, "32 >> 2 should be 8");

// ===== COMPARISON OPERATIONS =====
constexpr bool eq_test = (10 == 10);
static_assert(eq_test, "10 == 10 should be true");

constexpr bool ne_test = (10 != 20);
static_assert(ne_test, "10 != 20 should be true");

constexpr bool lt_test = (5 < 10);
static_assert(lt_test, "5 < 10 should be true");

constexpr bool le_test = (10 <= 10);
static_assert(le_test, "10 <= 10 should be true");

constexpr bool gt_test = (20 > 10);
static_assert(gt_test, "20 > 10 should be true");

constexpr bool ge_test = (15 >= 10);
static_assert(ge_test, "15 >= 10 should be true");

// ===== LOGICAL OPERATIONS =====
constexpr bool and_test = true && true;
static_assert(and_test, "true && true should be true");

constexpr bool or_test = false || true;
static_assert(or_test, "false || true should be true");

constexpr bool not_test = !false;
static_assert(not_test, "!false should be true");

// ===== COMPLEX EXPRESSIONS =====
constexpr int complex1 = (10 + 5) * 2 - 3;
static_assert(complex1 == 27, "(10 + 5) * 2 - 3 should be 27");

constexpr bool complex2 = (5 > 3) && (10 < 20);
static_assert(complex2, "complex boolean expression should be true");

constexpr int complex3 = ((8 << 1) + 4) / 5;
static_assert(complex3 == 4, "((8 << 1) + 4) / 5 should be 4");

// ===== VARIABLE REFERENCES =====
constexpr int ref_source = 100;
constexpr int ref_consumer = ref_source + 50;
static_assert(ref_consumer == 150, "variable reference should work");

// Chain of references
constexpr int chain1 = 10;
constexpr int chain2 = chain1 * 2;
constexpr int chain3 = chain2 + chain1;
static_assert(chain3 == 30, "chained references should work");

// ===== UNARY OPERATORS =====
constexpr int bitwise_not = ~0;
static_assert(bitwise_not == -1, "~0 should be -1");

constexpr bool logical_not = !false;
static_assert(logical_not, "!false should be true");

// ===== MIXED TYPE OPERATIONS =====
constexpr int mixed1 = 10 + 20 * 3 - 5;
static_assert(mixed1 == 65, "operator precedence should work");

constexpr bool mixed2 = (10 > 5) && (20 == 20);
static_assert(mixed2, "mixed comparison and logical ops should work");

// ===== CONSTINIT TESTS =====
constinit int init_literal = 42;

constinit int init_expr = 10 + 20;

constinit int init_ref = literal_int;

// ===== SIZEOF TESTS =====
constexpr int sizeof_int = sizeof(int);
static_assert(sizeof_int == 4, "sizeof(int) should be 4");

constexpr int sizeof_double = sizeof(double);
static_assert(sizeof_double == 8, "sizeof(double) should be 8");

// ===== EDGE CASES =====

// Zero values
constexpr int zero = 0;
static_assert(zero == 0, "zero should be 0");

// Negative values
constexpr int negative = -42;
static_assert(negative == -42, "negative values should work");

// Large values
constexpr int large = 2147483647;  // INT_MAX
static_assert(large == 2147483647, "large values should work");

// Hex literals
constexpr int hex_val = 0xFF;
static_assert(hex_val == 255, "hex literals should work");

// ===== SUMMARY =====
// This test demonstrates that the FlashCpp constexpr implementation supports:
// ✅ Basic constexpr variables
// ✅ All arithmetic operators (+, -, *, /, %)
// ✅ All bitwise operators (&, |, ^, <<, >>, ~)
// ✅ All comparison operators (==, !=, <, <=, >, >=)
// ✅ All logical operators (&&, ||, !)
// ✅ Variable references and chaining
// ✅ Complex expressions with correct precedence
// ✅ Constinit for static initialization
// ✅ sizeof operator
// ✅ Various literal formats (decimal, hex, negative)
// ✅ Zero and edge case values

void test() {
    // If this compiles, ALL constexpr tests passed! 🎉
}
