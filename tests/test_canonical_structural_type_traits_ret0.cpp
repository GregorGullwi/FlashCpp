// The structural [meta.unary.prop] family must be answered from the canonical
// type identity, not from the flat TypeCategory/pointer-level/array-dimension
// projection. Each shape below previously produced a different answer in the
// constant-expression path than in the code-generation path, because the
// constant-expression path classified arrays, records, enums, member pointers,
// and function pointers from flat fields while only pointers and arrays had a
// canonical answer.
struct Rec {
	int a;
	long b;
	void mfn();
};

enum class Scoped : unsigned short { A = 1 };
enum Plain { B = 2 };

template <typename T>
struct Box {
	T value;
	T* ptr;
};

void Rec::mfn() {}

int arr3[3];
extern int arrU[];
int arr2d[2][3];
Rec arrRec[2];

int (*pArr)[3];
int (*pArr2)[3][4];
int (*pFn)(int);
int (**ppFn)(int);
void (*pvf)(int);
int (*nnfn)(int) noexcept;
int (*(*ppArr)[3])(int);
int (*const pcArr)[3] = nullptr;

int elems[3];
int iv;
int& rInt = iv;
int&& rrInt = 3;
int& rRefEl = elems[1];
int (&rArr)[3] = elems;
int (*freeFnPtr)(int);
int (*&rpFn)(int) = freeFnPtr;

Rec rec;
Rec* prec;
Scoped sc;
Plain plain;
int Rec::* mop;
int (Rec::*mfp)(int);
int Box<int>::* boxptr;
Box<int> boxed;

char chr;
wchar_t wch;
double dbl;

// int[3]: an array, never a scalar, an arithmetic type, or fundamental.
static_assert(__is_array(decltype(arr3)));
static_assert(__is_bounded_array(decltype(arr3)));
static_assert(!__is_unbounded_array(decltype(arr3)));
static_assert(!__is_scalar(decltype(arr3)));
static_assert(!__is_arithmetic(decltype(arr3)));
static_assert(!__is_fundamental(decltype(arr3)));
static_assert(__is_compound(decltype(arr3)));
static_assert(__is_object(decltype(arr3)));

// int[]: an array of unknown bound.
static_assert(__is_array(decltype(arrU)));
static_assert(!__is_bounded_array(decltype(arrU)));
static_assert(__is_unbounded_array(decltype(arrU)));
static_assert(!__is_scalar(decltype(arrU)));
static_assert(!__is_arithmetic(decltype(arrU)));
static_assert(!__is_fundamental(decltype(arrU)));
static_assert(__is_compound(decltype(arrU)));

// int[2][3]: only the outer extent is a bound on the array itself.
static_assert(__is_array(decltype(arr2d)));
static_assert(__is_bounded_array(decltype(arr2d)));
static_assert(!__is_scalar(decltype(arr2d)));
static_assert(!__is_arithmetic(decltype(arr2d)));
static_assert(!__is_fundamental(decltype(arr2d)));
static_assert(__is_compound(decltype(arr2d)));

// Rec[2]: a record array is compound, not an enum.
static_assert(__is_array(decltype(arrRec)));
static_assert(__is_bounded_array(decltype(arrRec)));
static_assert(__is_compound(decltype(arrRec)));
static_assert(!__is_enum(decltype(arrRec)));

// int(*)[3] and int(*)[3][4]: pointer to array, never an array themselves.
static_assert(__is_pointer(decltype(pArr)));
static_assert(!__is_array(decltype(pArr)));
static_assert(!__is_bounded_array(decltype(pArr)));
static_assert(__is_scalar(decltype(pArr)));
static_assert(__is_pointer(decltype(pArr2)));
static_assert(!__is_array(decltype(pArr2)));
static_assert(__is_scalar(decltype(pArr2)));

// int* const: a const pointer object is still a scalar pointer.
static_assert(__is_pointer(decltype(pcArr)));
static_assert(!__is_array(decltype(pcArr)));
static_assert(__is_scalar(decltype(pcArr)));

// int(*)(int), int(**)(int), int(*)(int) noexcept, int(*(*)[3])(int): every
// function-pointer depth is one scalar pointer, not an array and not a function.
static_assert(__is_pointer(decltype(pFn)));
static_assert(!__is_array(decltype(pFn)));
static_assert(!__is_function(decltype(pFn)));
static_assert(__is_scalar(decltype(pFn)));
static_assert(__is_pointer(decltype(ppFn)));
static_assert(__is_scalar(decltype(ppFn)));
static_assert(__is_pointer(decltype(nnfn)));
static_assert(__is_scalar(decltype(nnfn)));
static_assert(__is_pointer(decltype(pvf)));
static_assert(__is_scalar(decltype(pvf)));
static_assert(__is_pointer(decltype(ppArr)));
static_assert(!__is_array(decltype(ppArr)));
static_assert(!__is_arithmetic(decltype(ppArr)));
static_assert(!__is_fundamental(decltype(ppArr)));
static_assert(!__is_integral(decltype(ppArr)));
static_assert(__is_compound(decltype(ppArr)));
static_assert(__is_scalar(decltype(ppArr)));

// int& and int&&: reference properties only; the referent decides nothing.
static_assert(__is_reference(decltype(rInt)));
static_assert(__is_lvalue_reference(decltype(rInt)));
static_assert(!__is_rvalue_reference(decltype(rInt)));
static_assert(!__is_scalar(decltype(rInt)));
static_assert(!__is_arithmetic(decltype(rInt)));
static_assert(!__is_fundamental(decltype(rInt)));
static_assert(__is_compound(decltype(rInt)));
static_assert(__is_reference(decltype(rrInt)));
static_assert(__is_rvalue_reference(decltype(rrInt)));
static_assert(!__is_lvalue_reference(decltype(rrInt)));

// int& to an element and int(&)[3]: a reference is not the array or the element.
static_assert(__is_reference(decltype(rRefEl)));
static_assert(!__is_array(decltype(rRefEl)));
static_assert(!__is_bounded_array(decltype(rRefEl)));
static_assert(!__is_scalar(decltype(rRefEl)));
static_assert(__is_reference(decltype(rArr)));
static_assert(__is_lvalue_reference(decltype(rArr)));
static_assert(!__is_array(decltype(rArr)));
static_assert(!__is_bounded_array(decltype(rArr)));

// int(*&)(int): a reference to a function pointer is a reference.
static_assert(__is_reference(decltype(rpFn)));
static_assert(__is_lvalue_reference(decltype(rpFn)));
static_assert(!__is_pointer(decltype(rpFn)));
static_assert(!__is_array(decltype(rpFn)));
static_assert(__is_compound(decltype(rpFn)));

// Records, enums, and member pointers.
static_assert(__is_object(decltype(rec)));
static_assert(__is_compound(decltype(rec)));
static_assert(!__is_scalar(decltype(rec)));
static_assert(__is_pointer(decltype(prec)));
static_assert(__is_scalar(decltype(prec)));
static_assert(__is_enum(decltype(sc)));
static_assert(__is_scalar(decltype(sc)));
static_assert(__is_object(decltype(sc)));
static_assert(__is_compound(decltype(sc)));
static_assert(__is_enum(decltype(plain)));
static_assert(__is_scalar(decltype(plain)));
static_assert(!__is_arithmetic(decltype(plain)));
static_assert(__is_member_object_pointer(decltype(mop)));
static_assert(!__is_pointer(decltype(mop)));
static_assert(__is_scalar(decltype(mop)));
static_assert(__is_member_function_pointer(decltype(mfp)));
static_assert(__is_scalar(decltype(mfp)));

// A class-template specialization is a complete class type, and its
// data-member pointer keeps the specialization owner.
static_assert(__is_object(decltype(boxed)));
static_assert(__is_compound(decltype(boxed)));
static_assert(!__is_scalar(decltype(boxed)));
static_assert(__is_member_object_pointer(decltype(boxptr)));
static_assert(__is_scalar(decltype(boxptr)));
static_assert(!__is_pointer(decltype(boxptr)));

// Native scalar types, including the character and wide-character types the
// compatibility classifier used to omit from `__is_integral`.
static_assert(__is_integral(decltype(chr)));
static_assert(__is_arithmetic(decltype(chr)));
static_assert(__is_scalar(decltype(chr)));
static_assert(__is_fundamental(decltype(chr)));
static_assert(!__is_compound(decltype(chr)));
static_assert(__is_integral(decltype(wch)));
static_assert(__is_arithmetic(decltype(wch)));
static_assert(__is_scalar(decltype(wch)));
static_assert(__is_floating_point(decltype(dbl)));
static_assert(__is_arithmetic(decltype(dbl)));
static_assert(__is_scalar(decltype(dbl)));
static_assert(__is_fundamental(decltype(dbl)));
static_assert(!__is_compound(decltype(dbl)));

// Direct type arguments, with no declaration to take decltype from.
static_assert(__is_void(void));
static_assert(__is_fundamental(void));
static_assert(!__is_object(void));
static_assert(!__is_compound(void));
static_assert(__is_integral(int));
static_assert(__is_floating_point(double));
static_assert(__is_pointer(int*));
static_assert(__is_array(int[3]));
static_assert(__is_bounded_array(int[3]));
static_assert(__is_unbounded_array(int[]));

// decltype(nullptr) is the only way to name the nullptr type, and it is a
// fundamental scalar object type.
static_assert(__is_nullptr(decltype(nullptr)));
static_assert(__is_scalar(decltype(nullptr)));
static_assert(__is_fundamental(decltype(nullptr)));
static_assert(__is_object(decltype(nullptr)));
static_assert(!__is_compound(decltype(nullptr)));
static_assert(!__is_arithmetic(decltype(nullptr)));

int main() {
	// The same classification must be produced when the trait is lowered as
	// ordinary code instead of folded as a constant expression.
	if (!__is_array(decltype(arr3))) {
		return 1;
	}
	if (!__is_bounded_array(decltype(arr3))) {
		return 2;
	}
	if (__is_scalar(decltype(arr3))) {
		return 3;
	}
	if (!__is_compound(decltype(arr3))) {
		return 4;
	}
	if (!__is_unbounded_array(decltype(arrU))) {
		return 5;
	}
	if (!__is_array(decltype(arr2d))) {
		return 6;
	}
	if (!__is_pointer(decltype(pArr)) || __is_array(decltype(pArr))) {
		return 7;
	}
	if (!__is_pointer(decltype(pFn)) || __is_array(decltype(pFn))) {
		return 8;
	}
	if (!__is_scalar(decltype(pFn))) {
		return 9;
	}
	if (!__is_pointer(decltype(ppFn))) {
		return 10;
	}
	if (!__is_scalar(decltype(nnfn))) {
		return 11;
	}
	if (!__is_scalar(decltype(pvf))) {
		return 12;
	}
	if (__is_arithmetic(decltype(ppArr)) || !__is_compound(decltype(ppArr))) {
		return 13;
	}
	if (!__is_reference(decltype(rInt)) || !__is_lvalue_reference(decltype(rInt))) {
		return 14;
	}
	if (!__is_rvalue_reference(decltype(rrInt))) {
		return 15;
	}
	if (!__is_reference(decltype(rRefEl)) || __is_array(decltype(rRefEl))) {
		return 16;
	}
	if (!__is_reference(decltype(rArr)) || __is_array(decltype(rArr))) {
		return 17;
	}
	if (!__is_reference(decltype(rpFn)) || __is_pointer(decltype(rpFn))) {
		return 18;
	}
	if (__is_scalar(decltype(rec)) || __is_enum(decltype(rec))) {
		return 19;
	}
	if (!__is_scalar(decltype(prec))) {
		return 20;
	}
	if (!__is_enum(decltype(sc)) || !__is_scalar(decltype(sc))) {
		return 21;
	}
	if (!__is_enum(decltype(plain)) || !__is_scalar(decltype(plain))) {
		return 22;
	}
	if (!__is_member_object_pointer(decltype(mop)) ||
		__is_pointer(decltype(mop))) {
		return 23;
	}
	if (!__is_member_function_pointer(decltype(mfp))) {
		return 24;
	}
	if (!__is_member_object_pointer(decltype(boxptr)) ||
		__is_pointer(decltype(boxptr))) {
		return 25;
	}
	if (__is_scalar(decltype(boxed)) || !__is_compound(decltype(boxed))) {
		return 26;
	}
	if (!__is_integral(decltype(wch)) || !__is_arithmetic(decltype(wch))) {
		return 27;
	}
	if (!__is_floating_point(decltype(dbl)) || !__is_fundamental(decltype(dbl))) {
		return 28;
	}
	return 0;
}
