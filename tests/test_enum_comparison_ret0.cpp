// Test enum comparison operations: ==, !=, <, >, <=, >=

enum Color { Red = 1,
			 Green = 2,
			 Blue = 3 };
enum class Status { Pending = 1,
					Running = 2,
					Done = 3 };

int test_enum_equality() {
	Color a = Red;
	Color b = Red;
	Color c = Green;

	int result = 0;
	if (a == b)
		result += 1;	 // true
	if (a != c)
		result += 2;	 // true
	if (a == c)
		result += 4;	 // false, shouldn't add

	return result;
}

int test_enum_inequality() {
	Color a = Red;
	Color b = Blue;

	int result = 0;
	if (a != b)
		result += 1;
	if (a != a)
		result += 2;	 // should be false

	return result;
}

int test_enum_less_than() {
	Color a = Red;
	Color b = Green;
	Color c = Blue;

	int result = 0;
	if (a < b)
		result += 1;	 // 1 < 2, true
	if (b < c)
		result += 2;	 // 2 < 3, true
	if (c < a)
		result += 4;	 // 3 < 1, false

	return result;
}

int test_enum_greater_than() {
	Color a = Red;
	Color b = Green;
	Color c = Blue;

	int result = 0;
	if (c > b)
		result += 1;	 // 3 > 2, true
	if (b > a)
		result += 2;	 // 2 > 1, true
	if (a > c)
		result += 4;	 // 1 > 3, false

	return result;
}

int test_enum_less_equal() {
	Color a = Red;
	Color b = Red;
	Color c = Green;

	int result = 0;
	if (a <= b)
		result += 1;	 // 1 <= 1, true
	if (a <= c)
		result += 2;	 // 1 <= 2, true
	if (c <= a)
		result += 4;	 // 2 <= 1, false

	return result;
}

int test_enum_greater_equal() {
	Color a = Red;
	Color b = Red;
	Color c = Green;

	int result = 0;
	if (b >= a)
		result += 1;	 // 1 >= 1, true
	if (c >= a)
		result += 2;	 // 2 >= 1, true
	if (a >= c)
		result += 4;	 // 1 >= 2, false

	return result;
}

int test_enum_class_comparison() {
	// Enum class comparisons require explicit casts or comparisons
	Status s1 = Status::Pending;
	Status s2 = Status::Done;

	int result = 0;
	if (s1 != s2)
		result += 1;
	if (static_cast<int>(s1) < static_cast<int>(s2))
		result += 2;

	return result;
}

int test_enum_vs_int_comparison() {
	// Unscoped enum can be compared with int
	Color c = Green;

	int result = 0;
	if (c == 2)
		result += 1;	 // 2 == 2, true
	if (c < 3)
		result += 2;	 // 2 < 3, true
	if (c > 1)
		result += 4;	 // 2 > 1, true
	if (c != 5)
		result += 8;	 // 2 != 5, true

	return result;
}

int main() {
	int result = 0;
	result += test_enum_equality();			// 3
	result += test_enum_inequality();		  // 1
	result += test_enum_less_than();			 // 3
	result += test_enum_greater_than();		// 3
	result += test_enum_less_equal();		  // 3
	result += test_enum_greater_equal();		 // 3
	result += test_enum_class_comparison();	// 3
	result += test_enum_vs_int_comparison();	 // 15

	// Expected: 3 + 1 + 3 + 3 + 3 + 3 + 3 + 15 = 34
	// If any test fails, bits will be missing
	return result == 34 ? 0 : result;
}
