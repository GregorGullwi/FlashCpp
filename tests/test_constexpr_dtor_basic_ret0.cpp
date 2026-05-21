// Test constexpr destructor: basic invocation, trivial empty body, and multiple
// objects in scope.  Verifies that constexpr destructors are called on scope exit
// and that return values are captured before the destructor runs.

struct WithState {
	int value;
	constexpr WithState(int v) : value(v) {}
	constexpr ~WithState() { value = -1; }
};

// The destructor sets value = -1.  After scope exit the object is gone; the return
// value is captured before the destructor runs, so it should still be 42.
constexpr int test_basic_dtor_after_return() {
	WithState s(42);
	return s.value;  // Captures 42; destructor runs after return expr is evaluated.
}

static_assert(test_basic_dtor_after_return() == 42,
	"Return value should be captured before destructor runs");

// Verify that a constexpr destructor with an empty body doesn't crash or error.
struct TrivialDtor {
	int x;
	constexpr TrivialDtor(int v) : x(v) {}
	constexpr ~TrivialDtor() {}
};

constexpr int test_trivial_dtor() {
	TrivialDtor t(7);
	return t.x;
}

static_assert(test_trivial_dtor() == 7, "Trivial constexpr destructor");

// Verify that multiple objects created in the same scope all have their
// destructors invoked without error.
struct Marker {
	int id;
	constexpr Marker(int i) : id(i) {}
	constexpr ~Marker() { id = 0; }
};

constexpr bool test_multiple_dtors_in_scope() {
	Marker a(1), b(2), c(3);
	return true;
}

static_assert(test_multiple_dtors_in_scope(), "Multiple dtors in scope");

int main() { return 0; }
