// Test typedef with pointer/reference declarators, east/west const, rvalue references
// Validates consume_pointer_ref_modifiers() handles all ptr-operator combinations

// Basic pointers and references
typedef int* IntPtr;
typedef int** IntPtrPtr;
typedef int& IntRef;
typedef int&& IntRRef;

// West const (traditional C++ style)
typedef const int* ConstIntPtr;              // pointer to const int
typedef int* const IntPtrConst;              // const pointer to int
typedef const int* const ConstIntPtrConst;   // const pointer to const int

// East const (alternative style)
typedef int const* IntConstPtr;              // pointer to const int (east)
typedef int const& IntConstRef;              // const lvalue reference (east)

// Mixed const in multiple pointers
typedef const int** ConstIntPtrPtr;          // ptr to ptr to const int
typedef int* const* IntPtrConstPtr;          // ptr to const-ptr to int

// Pointer-reference combination
typedef int*& IntPtrRef;

int main() {
	int val = 42;

	// Basic pointer
	IntPtr p = &val;
	if (*p != 42) return 1;

	// Pointer to pointer
	IntPtrPtr pp = &p;
	if (**pp != 42) return 2;

	// Lvalue reference
	IntRef r = val;
	if (r != 42) return 3;

	// Rvalue reference
	IntRRef rr = 42;
	if (rr != 42) return 4;

	// West const: pointer to const int
	ConstIntPtr wcp = &val;
	if (*wcp != 42) return 5;

	// East const: pointer to const int (same semantics as west)
	IntConstPtr ecp = &val;
	if (*ecp != 42) return 6;

	// Const pointer to int
	IntPtrConst pc = &val;
	if (*pc != 42) return 7;

	// Const pointer to const int
	ConstIntPtrConst cpc = &val;
	if (*cpc != 42) return 8;

	// East const reference
	IntConstRef ecr = val;
	if (ecr != 42) return 9;

	// Reference to pointer (*&)
	IntPtrRef pr = p;
	if (*pr != 42) return 10;

	// Ptr to ptr to const int
	const int* cip = &val;
	ConstIntPtrPtr cipp = &cip;
	if (**cipp != 42) return 11;

	// Ptr to const-ptr to int
	int* const pci = &val;
	IntPtrConstPtr pcip = &pci;
	if (**pcip != 42) return 12;

	return 0;
}
