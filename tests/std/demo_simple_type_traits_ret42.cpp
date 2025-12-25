// Simple demonstration of working type traits functionality
// This compiles quickly and demonstrates what FlashCpp can do today
// Expected return value: 42

// Minimal type trait implementations
template<typename T, T v>
struct integral_constant {
static constexpr T value = v;
constexpr operator T() const { return value; }
constexpr T operator()() const { return value; }
};

template<bool B>
using bool_constant = integral_constant<bool, B>;

using true_type = bool_constant<true>;
using false_type = bool_constant<false>;

int main() {
// Test integral_constant with int
integral_constant<int, 42> answer;
int x = answer;  // Uses conversion operator

if (x != 42) return 1;

// Test operator()
int y = answer();
if (y != 42) return 2;

// Test bool constant
bool b1 = true_type();
bool b2 = false_type();

if (!b1) return 3;
if (b2) return 4;

return 42;
}
