// Test variable template evaluation in constexpr context
// Verifies that variable templates with partial specializations
// can be correctly evaluated during static_assert.

template<typename T>
constexpr bool is_integral_custom_v = false;

template<>
constexpr bool is_integral_custom_v<int> = true;

template<>
constexpr bool is_integral_custom_v<long> = true;

static_assert(is_integral_custom_v<int>, "int should be integral");
static_assert(is_integral_custom_v<long>, "long should be integral");
static_assert(!is_integral_custom_v<float>, "float should not be integral");

int main() {
    return is_integral_custom_v<int> ? 1 : 0;
}
