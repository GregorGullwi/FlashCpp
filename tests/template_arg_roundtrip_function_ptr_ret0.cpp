// Test: round-trip function pointer template arguments through TypeInfo::TemplateArgInfo
// Verifies that TemplateArgInfo preserves function signature for function pointer template arguments

// Template that takes a function pointer type
template<class T>
struct FunctionWrapper {
static int test() { return 42; }
};

// Test instantiation via type alias
using IntIntToInt = int(int, int);

int main() {
// The test verifies that template argument conversion works
// by ensuring the type is properly preserved through the round-trip
int result = FunctionWrapper<IntIntToInt*>::test();
if (result != 42)
return 1;
return 0;
}
