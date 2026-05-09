// Test: template arguments with various qualifiers through TypeInfo::TemplateArgInfo
// Verifies that TemplateArgInfo preserves const/volatile/reference qualifiers

// Template that takes various qualified types
template<class T>
struct Wrapper {
static int test() { return 42; }
};

// Test with various qualifiers
int main() {
// const T
if (Wrapper<const int>::test() != 42) return 1;

// volatile T
if (Wrapper<volatile int>::test() != 42) return 2;

// const volatile T
if (Wrapper<const volatile int>::test() != 42) return 3;

// T&
int x = 0;
if (Wrapper<int&>::test() != 42) return 4;

// const T&
if (Wrapper<const int&>::test() != 42) return 5;

// T&&
if (Wrapper<int&&>::test() != 42) return 6;

// T*
if (Wrapper<int*>::test() != 42) return 7;

// const T*
if (Wrapper<const int*>::test() != 42) return 8;

// T* const
if (Wrapper<int* const>::test() != 42) return 9;

return 0;
}
