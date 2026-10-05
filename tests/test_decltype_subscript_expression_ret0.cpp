enum SubscriptIndex { ZeroIndex = 0 };

int main() {
	int values[3] = {0};
	int* pointer = values;
	int matrix[2][3] = {};
	int (*matrix_pointer)[3] = matrix;
	const int* const_pointer = values;
	using FromArray = decltype(values[1]);
	using FromPointer = decltype(pointer[0]);
	using FromReversedPointer = decltype(0[pointer]);
	using FromArrayOfArrays = decltype(matrix[0]);
	using FromPointerToArray = decltype(matrix_pointer[0]);
	using FromConstPointer = decltype(const_pointer[1]);
	using FromEnumIndex = decltype(pointer[ZeroIndex]);
	static_assert(__is_same(FromArray, int&), "array subscripting is an lvalue");
	static_assert(__is_same(FromPointer, int&), "pointer subscripting is an lvalue");
	static_assert(__is_same(FromReversedPointer, int&), "reversed pointer subscripting is an lvalue");
	static_assert(__is_same(FromArrayOfArrays, int (&)[3]), "array subscripting preserves the element array type");
	static_assert(__is_same(FromPointerToArray, int (&)[3]), "pointer subscripting preserves the pointee array type");
	static_assert(__is_same(FromConstPointer, const int&), "pointer subscripting preserves element cv-qualification");
	static_assert(__is_same(FromEnumIndex, int&), "unscoped enum indices are valid built-in subscripts");

	values[0] = 17;
	pointer[2] = 29;
	return values[0] == 17 && values[2] == 29 ? 0 : 1;
}
