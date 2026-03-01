// Regression test: codegen error recovery for non-struct member access
// Previously, generateMemberAccessIr returned {} for error cases, causing
// downstream SIGSEGV. Now it throws std::runtime_error which is caught by
// the per-function error recovery mechanism.
// This test uses <optional> which exercises the fixed code path.
#include <optional>

int main() {
	std::optional<int> opt = 42;
	return opt.has_value() ? 0 : 1;
}
