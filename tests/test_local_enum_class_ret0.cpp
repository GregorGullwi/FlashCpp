// Test: local enum class with explicit underlying type and type-qualified access
// Validates scoped enums declared inside function bodies work with the
// qualified access syntax (EnumClass::Enumerator).

enum class GlobalSeason { Spring = 10, Summer = 20, Fall = 30, Winter = 40 };

int test_local_enum_class_underlying() {
	enum class Priority : unsigned char { Low = 1, Medium = 2, High = 3 };
	Priority p = Priority::High;
	return (int)p - 3;  // expect 0
}

int test_local_enum_class_cast() {
	enum class Rank { Private = 1, Corporal = 2, Sergeant = 3 };
	Rank r = Rank::Sergeant;
	int v = static_cast<int>(r);
	return v - 3;  // expect 0
}

int test_local_enum_class_compare() {
	enum class Light { Red, Yellow, Green };
	Light l = Light::Green;
	int result = 0;
	if (l == Light::Green) {
		result = 0;
	} else {
		result = 1;
	}
	return result;
}

int test_global_and_local_enum_coexist() {
	// Global enum and local enum should not conflict
	GlobalSeason s = GlobalSeason::Summer;
	int sv = static_cast<int>(s);
	return sv - 20;  // expect 0
}

int main() {
	return test_local_enum_class_underlying()
	     + test_local_enum_class_cast()
	     + test_local_enum_class_compare()
	     + test_global_and_local_enum_coexist();
}
