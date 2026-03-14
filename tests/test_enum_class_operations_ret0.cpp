// Test: enum class (scoped enum) operations.
// Verifies that scoped enums work correctly with explicit casts,
// comparisons, and that the IR lowering preserves correct behavior.

enum class Status : int { Pending = 0, Running = 1, Done = 2, Error = 3 };

int main() {
	Status s = Status::Running;

	// Comparison
	if (s != Status::Running) return 1;

	// Explicit cast to underlying type
	int val = static_cast<int>(s);
	if (val != 1) return 2;

	// Cast from int to enum class
	Status s2 = static_cast<Status>(2);
	if (s2 != Status::Done) return 3;

	// Arithmetic on cast values
	int sum = static_cast<int>(Status::Running) + static_cast<int>(Status::Done);
	if (sum != 3) return 4;

	return 0;
}
