// Test flash_limits.h - Comprehensive numeric_limits test
// Expected return value: 42

#include "flash_minimal/flash_limits.h"

using namespace flash_std;

// Test compile-time checks with static_assert
static_assert(numeric_limits<bool>::is_specialized, "bool not specialized");
static_assert(numeric_limits<int>::is_specialized, "int not specialized");
static_assert(numeric_limits<float>::is_specialized, "float not specialized");

static_assert(numeric_limits<int>::is_signed, "int should be signed");
static_assert(!numeric_limits<unsigned int>::is_signed, "unsigned int should not be signed");

static_assert(numeric_limits<int>::is_integer, "int should be integer");
static_assert(!numeric_limits<float>::is_integer, "float should not be integer");

static_assert(numeric_limits<unsigned int>::is_modulo, "unsigned int should be modulo");
static_assert(!numeric_limits<int>::is_modulo, "int should not be modulo");

static_assert(numeric_limits<int>::is_bounded, "int should be bounded");
static_assert(numeric_limits<float>::is_bounded, "float should be bounded");

static_assert(numeric_limits<float>::has_infinity, "float should have infinity");
static_assert(!numeric_limits<int>::has_infinity, "int should not have infinity");

// Test min/max values
static_assert(numeric_limits<bool>::min() == false, "bool min failed");
static_assert(numeric_limits<bool>::max() == true, "bool max failed");

static_assert(numeric_limits<char>::min() == -128, "char min failed");
static_assert(numeric_limits<char>::max() == 127, "char max failed");

static_assert(numeric_limits<unsigned char>::min() == 0, "unsigned char min failed");
static_assert(numeric_limits<unsigned char>::max() == 255, "unsigned char max failed");

static_assert(numeric_limits<short>::min() == -32768, "short min failed");
static_assert(numeric_limits<short>::max() == 32767, "short max failed");

static_assert(numeric_limits<int>::min() == -2147483648, "int min failed");
static_assert(numeric_limits<int>::max() == 2147483647, "int max failed");

static_assert(numeric_limits<unsigned int>::min() == 0, "unsigned int min failed");
static_assert(numeric_limits<unsigned int>::max() == 4294967295u, "unsigned int max failed");

// Test digits
static_assert(numeric_limits<bool>::digits == 1, "bool digits failed");
static_assert(numeric_limits<char>::digits == 7, "char digits failed");
static_assert(numeric_limits<int>::digits == 31, "int digits failed");
static_assert(numeric_limits<unsigned int>::digits == 32, "unsigned int digits failed");

// Test digits10
static_assert(numeric_limits<int>::digits10 == 9, "int digits10 failed");
static_assert(numeric_limits<float>::digits10 == 6, "float digits10 failed");
static_assert(numeric_limits<double>::digits10 == 15, "double digits10 failed");

// Runtime tests
int test_bool_limits() {
	if (numeric_limits<bool>::min() != false) return 1;
	if (numeric_limits<bool>::max() != true) return 2;
	return 0;
}

int test_int_limits() {
	if (numeric_limits<int>::min() >= 0) return 1;
	if (numeric_limits<int>::max() <= 0) return 2;
	if (numeric_limits<int>::lowest() != numeric_limits<int>::min()) return 3;
	return 0;
}

int test_unsigned_limits() {
	if (numeric_limits<unsigned int>::min() != 0) return 1;
	if (numeric_limits<unsigned int>::max() <= 0) return 2;
	return 0;
}

int test_float_limits() {
	if (numeric_limits<float>::min() <= 0.0f) return 1;
	if (numeric_limits<float>::max() <= 0.0f) return 2;
	if (numeric_limits<float>::lowest() >= 0.0f) return 3;
	return 0;
}

int test_double_limits() {
	if (numeric_limits<double>::min() <= 0.0) return 1;
	if (numeric_limits<double>::max() <= 0.0) return 2;
	if (numeric_limits<double>::lowest() >= 0.0) return 3;
	return 0;
}

int test_properties() {
	// Test is_signed
	if (!numeric_limits<int>::is_signed) return 1;
	if (numeric_limits<unsigned int>::is_signed) return 2;
	if (!numeric_limits<float>::is_signed) return 3;
	
	// Test is_integer
	if (!numeric_limits<int>::is_integer) return 4;
	if (numeric_limits<float>::is_integer) return 5;
	
	// Test is_exact
	if (!numeric_limits<int>::is_exact) return 6;
	if (numeric_limits<float>::is_exact) return 7;
	
	// Test has_infinity
	if (numeric_limits<int>::has_infinity) return 8;
	if (!numeric_limits<float>::has_infinity) return 9;
	if (!numeric_limits<double>::has_infinity) return 10;
	
	return 0;
}

int test_char_types() {
	if (numeric_limits<char>::min() >= 0) return 1;
	if (numeric_limits<signed char>::min() >= 0) return 2;
	if (numeric_limits<unsigned char>::min() != 0) return 3;
	
	if (numeric_limits<char>::max() != 127) return 4;
	if (numeric_limits<unsigned char>::max() != 255) return 5;
	
	return 0;
}

int test_long_types() {
	if (numeric_limits<long>::min() >= 0) return 1;
	if (numeric_limits<unsigned long>::min() != 0) return 2;
	if (numeric_limits<long long>::min() >= 0) return 3;
	if (numeric_limits<unsigned long long>::min() != 0) return 4;
	
	return 0;
}

int main() {
	int result = 0;
	
	result = test_bool_limits();
	if (result != 0) return 1;
	
	result = test_int_limits();
	if (result != 0) return 2;
	
	result = test_unsigned_limits();
	if (result != 0) return 3;
	
	result = test_float_limits();
	if (result != 0) return 4;
	
	result = test_double_limits();
	if (result != 0) return 5;
	
	result = test_properties();
	if (result != 0) return 6;
	
	result = test_char_types();
	if (result != 0) return 7;
	
	result = test_long_types();
	if (result != 0) return 8;
	
	// All tests passed
	return 42;
}
