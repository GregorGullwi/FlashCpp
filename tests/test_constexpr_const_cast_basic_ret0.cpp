constexpr int value = 42;
constexpr const int* ptr = &value;

struct ConstexprConstCastPoint {
	int x;
	constexpr ConstexprConstCastPoint(int v) : x(v) {}
};

constexpr ConstexprConstCastPoint point(7);
constexpr const ConstexprConstCastPoint* point_ptr = &point;

// Pointer-based const_cast: remove const from pointer
constexpr int read_through_const_cast_ptr() {
	return *const_cast<int*>(ptr);
}

constexpr int read_through_const_cast_struct_ptr() {
	return const_cast<ConstexprConstCastPoint*>(point_ptr)->x;
}

// Reference-based const_cast: add reference qualifier
constexpr int read_via_const_ref_cast() {
	return const_cast<const int&>(value);
}

// Reference-based const_cast: remove const through reference
constexpr int read_via_mutable_ref_cast() {
	const int local = 99;
	return const_cast<int&>(local);
}

// Reference-based const_cast on struct: const T& -> T&
constexpr int read_struct_via_ref_cast() {
	return const_cast<ConstexprConstCastPoint&>(
		static_cast<const ConstexprConstCastPoint&>(point)).x;
}

static_assert(read_through_const_cast_ptr() == 42);
static_assert(read_through_const_cast_struct_ptr() == 7);
static_assert(read_via_const_ref_cast() == 42);
static_assert(read_via_mutable_ref_cast() == 99);
static_assert(read_struct_via_ref_cast() == 7);

int main() {
	return 0;
}
