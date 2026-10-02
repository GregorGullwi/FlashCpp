// A delegating constructor lowers to an early return; that return must still
// close the constructor's function scope. If it leaks, a following global is
// misclassified as a function local and loses its global storage.
struct Delegating {
	int value;

	constexpr Delegating(int v) : value(v) {}
	constexpr Delegating() : Delegating(42) {}
};

int global_after = 7;

int main() {
	if (global_after != 7) return 1;
	constexpr Delegating d{};
	if (d.value != 42) return 2;
	return 0;
}
