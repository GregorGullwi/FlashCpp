// Test that enum class types are mangled correctly (not silently as int).
// If both parameter and call site use the same correct mangling, linking succeeds.
// Returns 0 on success.

enum class Direction { North = 1,
					   South = 2,
					   East = 3,
					   West = 4 };

int value_of(Direction d) {
	return static_cast<int>(d);
}

int main() {
	return value_of(Direction::East) - 3; // 3 - 3 = 0
}
