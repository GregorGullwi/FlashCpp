// Test: top-level using alias with dependent non-type argument chain

template<typename T, T v>
struct integral_constant {
static constexpr T value = v;
};

template<bool B>
using bool_constant = integral_constant<bool, B>;

template<int N>
using int_constant = integral_constant<int, N>;

// Top-level aliases using template aliases
using forty_two = int_constant<42>;
using truth = bool_constant<true>;
using falsehood = bool_constant<false>;

int main() {
int result = 0;
if (forty_two::value == 42) result += 30;
if (truth::value == true)   result += 6;
if (falsehood::value == false) result += 6;
return result;  // Expected: 42
}
