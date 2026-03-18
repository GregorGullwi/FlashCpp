// Test: two functions define local enum class types with the same name "Priority"
// but different enumerator values.  Since gTypesByName uses bare names and
// emplace is a no-op on duplicate keys, the second function's enum class will
// collide with the first — gTypesByName["Priority"] always points to the first
// function's TypeInfo.  generateQualifiedIdentifierIr resolves Priority::High
// via gTypesByName, so the second function gets the wrong enumerator value.
//
// Expected: foo() returns 10, bar() returns 99.
// main() returns foo() + bar() - 109 == 0 on success.

int foo() {
	enum class Priority { Low = 1, Medium = 5, High = 10 };
	Priority p = Priority::High;
	return static_cast<int>(p);  // expect 10
}

int bar() {
	enum class Priority { Low = 50, Medium = 75, High = 99 };
	Priority p = Priority::High;
	return static_cast<int>(p);  // expect 99
}

int main() {
	return foo() + bar() - 109;
}
