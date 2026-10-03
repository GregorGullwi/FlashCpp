// Reference-to-array type-ids must work consistently in alias-template
// targets, explicit type template arguments, and reference non-type arguments.
template <class T, int N>
using ArrayRef = T (&)[N];

template <class T, int Rows, int Columns>
using MatrixRef = T (&)[Rows][Columns];

template <class T>
struct TypeIdentity {
	using type = T;
	static constexpr int size = sizeof(T);
};

template <class T>
int type_id_size() {
	return sizeof(T);
}

int global_values[3] = {4, 5, 6};

template <int (&R)[3]>
struct ArrayReferenceTag {
	static constexpr int value = 1;
};

template <>
struct ArrayReferenceTag<global_values> {
	static constexpr int value = 10;
};

static_assert(__is_same(ArrayRef<int, 3>, int (&)[3]));
static_assert(__is_same(MatrixRef<int, 2, 3>, int (&)[2][3]));
static_assert(__is_same(TypeIdentity<int (&)[3]>::type, int (&)[3]));

int main() {
	int local_values[3] = {1, 2, 3};
	ArrayRef<int, 3> alias = local_values;
	alias[1] = 8;
	if (local_values[1] != 8) return 1;
	int local_matrix[2][3] = {{1, 2, 3}, {4, 5, 6}};
	MatrixRef<int, 2, 3> matrix_alias = local_matrix;
	matrix_alias[1][2] = 9;
	if (local_matrix[1][2] != 9) return 6;

	if (TypeIdentity<int (&)[3]>::size != sizeof(int[3])) return 2;
	if (type_id_size<int (&)[3]>() != sizeof(int[3])) return 3;

	if (ArrayReferenceTag<global_values>::value != 10) return 4;
	global_values[1] = 10;
	if (ArrayReferenceTag<global_values>::value != 10) return 5;
	return 0;
}
