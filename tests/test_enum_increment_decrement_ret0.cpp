// Test enum increment/decrement via explicit arithmetic
// Note: Direct ++/-- on enums may not be supported yet - this tests equivalent operations.

enum Color : int { Red = 1,
				   Green = 2,
				   Blue = 3 };
enum class Status : int { Pending = 0,
						  Running = 1,
						  Done = 2 };

// Manual increment equivalent
Color nextColor(Color c) {
	if (c == Red)
		return Green;
	if (c == Green)
		return Blue;
	return Blue;	 // Blue stays Blue (or could wrap)
}

// Manual decrement equivalent
Color prevColor(Color c) {
	if (c == Blue)
		return Green;
	if (c == Green)
		return Red;
	return Red;	// Red stays Red (or could wrap)
}

int test_enum_manual_increment() {
	Color c = Red;
	c = nextColor(c);  // Should be Green
	if (c != Green)
		return 1;
	c = nextColor(c);  // Should be Blue
	if (c != Blue)
		return 2;
	return 0;
}

int test_enum_manual_decrement() {
	Color c = Blue;
	c = prevColor(c);  // Should be Green
	if (c != Green)
		return 1;
	c = prevColor(c);  // Should be Red
	if (c != Red)
		return 2;
	return 0;
}

int test_enum_class_manual_increment() {
	Status s = Status::Pending;
	s = static_cast<Status>(static_cast<int>(s) + 1);
	if (s != Status::Running)
		return 4;
	s = static_cast<Status>(static_cast<int>(s) + 1);
	if (s != Status::Done)
		return 8;
	return 0;
}

int test_enum_class_manual_decrement() {
	Status s = Status::Done;
	s = static_cast<Status>(static_cast<int>(s) - 1);
	if (s != Status::Running)
		return 16;
	s = static_cast<Status>(static_cast<int>(s) - 1);
	if (s != Status::Pending)
		return 32;
	return 0;
}

int main() {
	int result = 0;
	result += test_enum_manual_increment();	// 0
	result += test_enum_manual_decrement();	// 0
	result += test_enum_class_manual_increment(); // 0
	result += test_enum_class_manual_decrement(); // 0

	return result;
}
