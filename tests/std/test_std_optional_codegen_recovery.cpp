// Regression test: member access on <optional> must compile and run.
// Previously, generateMemberAccessIr returned {} for error cases, causing
// downstream SIGSEGV. Member-access failures now throw InternalError and
// convert() surfaces them as CompileError with the enclosing function name.
// Blocked on <optional> parsing today (structural class-type NTTP in MSVC STL);
// see README_STANDARD_HEADERS.md.
#include <optional>

int main() {
	std::optional<int> opt = 42;
	return opt.has_value() ? 0 : 1;
}
