// Phase 5 regression: constexpr function-call evaluation should respect the
// parser-stored qualified target instead of re-looking up the raw name.

constexpr int helper() {
	return 5;
}

namespace math {
	constexpr int helper() {
		return 42;
	}
}

constexpr int result = math::helper();
static_assert(result == 42, "qualified constexpr call should use math::helper");

int main() {
	return result;
}