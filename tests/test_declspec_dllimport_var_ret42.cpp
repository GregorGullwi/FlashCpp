// Regression test: __declspec(dllimport) on a variable declaration must
// not allocate local storage — the symbol is provided by the importing DLL.
//
// Before the fix, FlashCpp silently discarded the dllimport linkage on
// variable declarations and emitted a strong definition, causing
// duplicate-definition errors.
//
// After the fix, the dllimport declaration is emitted as an undefined
// external reference, resolved by the definition in the helper .c file.

__declspec(dllimport) int testDllImportVar;

int main() {
	return testDllImportVar;
}
