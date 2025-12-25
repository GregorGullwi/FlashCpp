// Test flash_type_traits.h - Simplified test
// Expected return value: 42

#include "flash_minimal/flash_type_traits.h"

using namespace flash_std;

struct TestClass {};
enum TestEnum { A, B };
union TestUnion { int i; float f; };

// Test runtime behavior
int main() {
// Test integral_constant operator()
integral_constant<int, 10> ic;
int val1 = ic();  // Call operator()
int val2 = ic;    // Use conversion operator

if (val1 != 10 || val2 != 10) return 1;

// Test type traits at runtime
if (!is_same<int, int>::value) return 2;
if (is_same<int, float>::value) return 3;

if (!is_integral<int>::value) return 4;
if (is_integral<float>::value) return 5;

if (!is_floating_point<float>::value) return 6;
if (is_floating_point<int>::value) return 7;

if (!is_pointer<int*>::value) return 8;
if (is_pointer<int>::value) return 9;

if (!is_class<TestClass>::value) return 10;
if (is_class<int>::value) return 11;

if (!is_enum<TestEnum>::value) return 12;
if (is_enum<int>::value) return 13;

if (!is_union<TestUnion>::value) return 14;
if (is_union<TestClass>::value) return 15;

if (!is_const<const int>::value) return 16;
if (is_const<int>::value) return 17;

if (!is_volatile<volatile int>::value) return 18;
if (is_volatile<int>::value) return 19;

if (!is_signed<int>::value) return 20;
if (is_signed<unsigned int>::value) return 21;

if (!is_unsigned<unsigned int>::value) return 22;
if (is_unsigned<int>::value) return 23;

// Test type modifications
if (!is_same<remove_const_t<const int>, int>::value) return 24;
if (!is_same<remove_volatile_t<volatile int>, int>::value) return 25;
if (!is_same<remove_cv_t<const volatile int>, int>::value) return 26;

if (!is_same<remove_reference_t<int&>, int>::value) return 27;
if (!is_same<remove_reference_t<int&&>, int>::value) return 28;

if (!is_same<remove_pointer_t<int*>, int>::value) return 29;

if (!is_same<add_const_t<int>, const int>::value) return 30;
if (!is_same<add_volatile_t<int>, volatile int>::value) return 31;

// Test conditional
if (!is_same<conditional_t<true, int, float>, int>::value) return 32;
if (!is_same<conditional_t<false, int, float>, float>::value) return 33;

// Test composite categories
if (!is_arithmetic<int>::value) return 34;
if (!is_arithmetic<float>::value) return 35;
if (is_arithmetic<TestClass>::value) return 36;

if (!is_fundamental<int>::value) return 37;
if (!is_fundamental<void>::value) return 38;
if (is_fundamental<TestClass>::value) return 39;

if (!is_compound<TestClass>::value) return 40;
if (is_compound<int>::value) return 41;

// All tests passed
return 42;
}
