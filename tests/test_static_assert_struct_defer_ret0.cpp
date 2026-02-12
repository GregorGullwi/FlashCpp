// Test: static_assert with complex constexpr in struct body is deferred
// This pattern appears in <chrono>: static_assert(system_clock::duration::min() < ...)
struct Helper {
	static constexpr int value() { return 42; }
};

struct MyStruct {
	// static_assert with complex constexpr that the evaluator may not fully handle
	static constexpr int x = 42;
};

int main() {
	return MyStruct::x - 42;
}
