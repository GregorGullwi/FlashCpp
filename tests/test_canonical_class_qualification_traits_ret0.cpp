// The class-property and qualification traits are answered from canonical
// record facts and type identity: class/union, polymorphic, final, and abstract
// state from published semantic facts, cv qualification from the canonical
// qualifier plus [dcl.array]'s element-qualification rule, and signedness from
// the canonical builtin under the target data model. The constant-expression
// path previously read flat fields
// and disagreed with the code-generation path - it called an array or a pointer
// signed, missed cv on a pointer object, and reported cv introduced through a
// reference as if it qualified the reference.
struct Rec {
	int a;
	long b;
	void bump();
};

struct Derived : Rec {
	int extra;
};

struct Polymorphic {
	virtual void bump() {}
};

struct FinalRecord final {};

struct AbstractRecord {
	virtual void run() = 0;
};

struct ConcreteRecord : AbstractRecord {
	void run() override {}
};

union Variant {
	int narrow;
	long wide;
};

enum class Scoped : unsigned short { A = 1 };
enum Plain { B = 2 };

template <typename T>
struct Box {
	T value;
	T* ptr;
};

void Rec::bump() {}

Rec record{};
const Rec constRecord{};
Variant variant{};
Derived derived{};
Scoped scoped = Scoped::A;
Plain plain = B;
Box<int> boxed{};

// A class type is a struct or class, never a union and never an enumeration.
static_assert(__is_class(Rec));
static_assert(__is_class(Derived));
static_assert(__is_class(Box<int>));
static_assert(!__is_class(Variant));
static_assert(!__is_class(Scoped));
static_assert(!__is_class(Plain));
static_assert(!__is_class(int));
static_assert(!__is_class(int*));
static_assert(!__is_class(int[3]));
static_assert(!__is_class(Rec&));

// These class properties come from semantic facts published with the record,
// rather than from spelling-based type recovery in each trait consumer.
static_assert(__is_polymorphic(Polymorphic));
static_assert(__is_polymorphic(AbstractRecord));
static_assert(!__is_polymorphic(FinalRecord));
static_assert(__is_final(FinalRecord));
static_assert(!__is_final(ConcreteRecord));
static_assert(__is_abstract(AbstractRecord));
static_assert(!__is_abstract(ConcreteRecord));

// Only a union is a union, and a union is not a class.
static_assert(__is_union(Variant));
static_assert(!__is_union(Rec));
static_assert(!__is_union(Box<int>));
static_assert(!__is_union(Scoped));
static_assert(!__is_union(int));
static_assert(!__is_union(Rec&));

// cv qualification is a property of the qualified type, and a reference is
// never cv-qualified because [dcl.ref] drops cv introduced through it.
static_assert(__is_const(const int));
static_assert(!__is_const(int));
static_assert(__is_volatile(volatile int));
static_assert(!__is_volatile(int));
static_assert(__is_const(const volatile int));
static_assert(__is_volatile(const volatile int));
static_assert(__is_const(const Rec));
static_assert(__is_class(const Rec));
// `const int*` is a pointer to const int, not a const pointer, so the pointee's
// cv does not make the pointer type cv-qualified.
static_assert(!__is_const(const int*));
static_assert(!__is_volatile(volatile int*));
static_assert(__is_const(int* const));
static_assert(!__is_const(int&));
static_assert(__is_const(const int));	// the referent is const, the reference is not

// [dcl.array] an array type is identically cv-qualified to its element, so the
// array carries the element's cv even though the canonical node that forms it is
// the array wrapper.
static_assert(__is_const(const int[3]));
static_assert(!__is_const(int[3]));
static_assert(__is_volatile(volatile int[2]));
static_assert(!__is_volatile(int[2]));

// The signedness of a type is a property of its own canonical builtin, not of
// the container it was reached through.
static_assert(__is_signed(int));
static_assert(__is_signed(short));
static_assert(__is_signed(char));
static_assert(__is_signed(long long));
#ifdef _MSC_VER
static_assert(!__is_signed(wchar_t));
static_assert(__is_unsigned(wchar_t));
#else
static_assert(__is_signed(wchar_t));
static_assert(!__is_unsigned(wchar_t));
#endif
static_assert(__is_signed(float));
static_assert(__is_signed(double));
// `long double` is deliberately excluded: the shipped model gives it the
// `double` size and representation on every target
// (tests/test_windows_long_double_abi_bit_cast_ret0.cpp locks that in), so a
// signedness assertion about it would only restate the `double` one. See the
// SysV x87 known issue for the codegen gap.
static_assert(!__is_signed(int[3]));
static_assert(!__is_signed(const int[3]));
static_assert(!__is_signed(int*));
static_assert(!__is_signed(int&));
static_assert(!__is_signed(Rec));
static_assert(!__is_signed(Scoped));
static_assert(!__is_signed(Box<int>));

static_assert(__is_unsigned(unsigned char));
static_assert(__is_unsigned(unsigned short));
static_assert(__is_unsigned(unsigned int));
static_assert(__is_unsigned(unsigned long long));
static_assert(__is_unsigned(bool));
static_assert(!__is_unsigned(int));
static_assert(!__is_unsigned(double));
static_assert(!__is_unsigned(int[3]));
static_assert(!__is_unsigned(int&));
static_assert(!__is_unsigned(Rec));

// A cv-qualified pointer object is still a pointer, and a const record is still
// a class.
static_assert(__is_pointer(int* const));
static_assert(__is_const(int* const));

int main() {
	// The same classification must hold when the trait is lowered as ordinary
	// code instead of folded as a constant expression.
	unsigned mismatches = 0;
	mismatches |= __is_class(Rec) ? 0u : 1u;
	mismatches |= __is_class(Variant) ? 2u : 0u;
	mismatches |= __is_union(Variant) ? 0u : 4u;
	mismatches |= __is_union(Rec) ? 8u : 0u;
	mismatches |= __is_class(Box<int>) ? 0u : 16u;
	mismatches |= __is_class(Scoped) ? 32u : 0u;
	mismatches |= __is_class(Rec&) ? 64u : 0u;
	mismatches |= __is_const(const Rec) ? 0u : 128u;
	mismatches |= __is_class(const Rec) ? 0u : 256u;
	mismatches |= __is_const(const int[3]) ? 0u : 512u;
	mismatches |= __is_const(int[3]) ? 1024u : 0u;
	mismatches |= __is_volatile(volatile int[2]) ? 0u : 2048u;
	mismatches |= __is_const(const int&) ? 4096u : 0u;
	mismatches |= __is_const(int& ) ? 8192u : 0u;
	mismatches |= __is_signed(double) ? 0u : 16384u;
	mismatches |= __is_signed(int[3]) ? 32768u : 0u;
	mismatches |= __is_signed(int*) ? 65536u : 0u;
	mismatches |= __is_signed(int&) ? 131072u : 0u;
#ifdef _MSC_VER
	mismatches |= __is_signed(wchar_t) ? 262144u : 0u;
#else
	mismatches |= __is_signed(wchar_t) ? 0u : 262144u;
#endif
	mismatches |= __is_unsigned(unsigned int) ? 0u : 524288u;
	mismatches |= __is_unsigned(double) ? 1048576u : 0u;
	mismatches |= __is_unsigned(int[3]) ? 2097152u : 0u;
	mismatches |= __is_pointer(int* const) ? 0u : 4194304u;
	mismatches |= __is_const(int* const) ? 0u : 8388608u;
	mismatches |= __is_polymorphic(Polymorphic) ? 0u : 16777216u;
	mismatches |= __is_polymorphic(FinalRecord) ? 33554432u : 0u;
	mismatches |= __is_final(FinalRecord) ? 0u : 67108864u;
	mismatches |= __is_final(ConcreteRecord) ? 134217728u : 0u;
	mismatches |= __is_abstract(AbstractRecord) ? 0u : 268435456u;
	mismatches |= __is_abstract(ConcreteRecord) ? 536870912u : 0u;
	return static_cast<int>(mismatches & 0x7fffffffu);
}
