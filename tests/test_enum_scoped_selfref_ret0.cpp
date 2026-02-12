// Test: scoped enum values referencing other enumerators
// Per C++ §9.7.1/2, enumerator names are in scope within the enum body
// so later enumerators can reference earlier ones.
enum class Flags : int {
	Read = 1,
	Write = 2,
	Execute = 4,
	ReadWrite = Read | Write,
	All = Read | Write | Execute
};

int main() {
	if (static_cast<int>(Flags::ReadWrite) != 3) return 1;
	if (static_cast<int>(Flags::All) != 7) return 2;
	return 0;
}
