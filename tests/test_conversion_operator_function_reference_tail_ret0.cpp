void target() {}
using Function = void (&)();
using Pointer = void (*)();
struct Source { operator Function() const { return target; } };
int main() {
	Pointer function = Source{};
	if (function != &target)
		return 1;
	function();
	return 0;
}
