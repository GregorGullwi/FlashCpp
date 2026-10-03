typedef int (*IntArrayPointerTypedef)[3];
using IntArrayPointerAlias = int (*)[3];
using ChainedIntArrayPointer = IntArrayPointerAlias;
using UnsignedShortMatrixPointer = unsigned short (*)[2][3];

struct ArrayElement {
	int value;
};

using StructArrayPointer = ArrayElement (*)[2];
static_assert(__is_same(ArrayElement, ArrayElement));
static_assert(__is_same(ArrayElement (*)[2], ArrayElement (*)[2]));

struct ScopedArrayPointerAliases {
	typedef int (*TypedefPointer)[3];
	using UsingPointer = int (*)[3];
};

static_assert(__is_pointer(IntArrayPointerTypedef));
static_assert(!__is_array(IntArrayPointerTypedef));
static_assert(__is_pointer(IntArrayPointerAlias));
static_assert(!__is_array(IntArrayPointerAlias));
static_assert(__is_same(IntArrayPointerTypedef, int (*)[3]));
static_assert(__is_same(IntArrayPointerAlias, int (*)[3]));
static_assert(__is_same(ChainedIntArrayPointer, int (*)[3]));
static_assert(__is_same(UnsignedShortMatrixPointer, unsigned short (*)[2][3]));
static_assert(__is_same(StructArrayPointer, ArrayElement (*)[2]));
static_assert(__is_same(ScopedArrayPointerAliases::TypedefPointer, int (*)[3]));
static_assert(__is_same(ScopedArrayPointerAliases::UsingPointer, int (*)[3]));

int main() {
	int values[3] = {11, 17, 23};
	IntArrayPointerTypedef typedef_pointer = &values;
	IntArrayPointerAlias alias_pointer = &values;
	unsigned short matrix[2][3] = {{2, 3, 5}, {7, 11, 13}};
	UnsignedShortMatrixPointer matrix_pointer = &matrix;
	ArrayElement elements[2] = {{19}, {29}};
	StructArrayPointer struct_pointer = &elements;
	if (sizeof(typedef_pointer) != sizeof(void*)) return 1;
	if (sizeof(alias_pointer) != sizeof(void*)) return 2;
	if ((*typedef_pointer)[1] != 17) return 3;
	if ((*alias_pointer)[2] != 23) return 4;
	if ((*matrix_pointer)[1][2] != 13) return 5;
	if ((*struct_pointer)[1].value != 29) return 6;
	(*typedef_pointer)[0] = 31;
	(*alias_pointer)[2] = 47;
	(*matrix_pointer)[0][1] = 37;
	(*struct_pointer)[0].value = 41;
	return values[0] == 31 && values[2] == 47 &&
		matrix[0][1] == 37 && elements[0].value == 41 ? 0 : 7;
}
