// Test: local (function-scoped) enum declarations
// Validates that enum, enum class, and storage-class-prefixed enums can be
// declared inside function bodies and used as local types.

int test_local_unscoped_enum() {
	enum Color { Red = 1,
				 Green = 2,
				 Blue = 3 };
	Color c = Green;
	return c - 2;  // expect 0
}

int test_local_enum_class() {
	enum class Fruit { Apple,
					   Banana,
					   Cherry };
	Fruit f = Fruit::Cherry;
	return (int)f - 2;  // expect 0
}

int test_local_enum_variable_after_brace() {
	enum Direction { North = 10,
					 South = 20,
					 East = 30,
					 West = 40 };
	Direction d = East;
	int val = d;
	return val - 30;	 // expect 0
}

int test_local_enum_in_if() {
	enum Status { OK = 0,
				  Error = 1 };
	Status s = OK;
	if (s == OK) {
		return 0;
	}
	return 1;
}

int test_local_enum_arithmetic() {
	enum Size { Small = 1,
				Medium = 2,
				Large = 3 };
	int total = Small + Medium + Large;
	return total - 6;  // expect 0
}

int main() {
	return test_local_unscoped_enum() + test_local_enum_class() + test_local_enum_variable_after_brace() + test_local_enum_in_if() + test_local_enum_arithmetic();
}
