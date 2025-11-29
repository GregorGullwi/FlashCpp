// Test file for __countof_helper construct
// This demonstrates parsing of a template function that returns pointer to array
// Pattern: char (*__countof_helper(T (&_Array)[N]))[N];
//
// This pattern is used by Windows SDK for type-safe array size macros.
// The construct involves:
// 1. A template function with two parameters: type T and non-type size_t N
// 2. Return type is char (*)[N] - pointer to array of N chars
// 3. Parameter is T (&_Array)[N] - reference to array of T with size N

#define _UNALIGNED

// Template function declaration with complex declarator:
// - Return type: char (*)[_SizeOfArray] (pointer to char array of _SizeOfArray elements)
// - Function name: __countof_helper  
// - Parameter: reference to array of _CountofType with _SizeOfArray elements
template <typename _CountofType, size_t _SizeOfArray>
char (*__countof_helper(_UNALIGNED _CountofType (&_Array)[_SizeOfArray]))[_SizeOfArray];

// The macro uses sizeof to get the array size at compile time
// sizeof(*__countof_helper(arr)) returns sizeof(char[N]) which equals N
#define __crt_countof(_Array) (sizeof(*__countof_helper(_Array)) + 0)

int main() {
    // This demonstrates that the template declaration parses correctly
    // The actual usage (with template argument deduction) is a separate feature
    return 0;
}
