constexpr int value = 42;
constexpr const int* ptr = &value;

struct ConstexprConstCastPoint {
	int x;
	constexpr ConstexprConstCastPoint(int v) : x(v) {}
};

constexpr ConstexprConstCastPoint point(7);
constexpr const ConstexprConstCastPoint* point_ptr = &point;

constexpr int read_through_const_cast_ptr() {
	return *const_cast<int*>(ptr);
}

constexpr int read_through_const_cast_struct_ptr() {
	return const_cast<ConstexprConstCastPoint*>(point_ptr)->x;
}

static_assert(read_through_const_cast_ptr() == 42);
static_assert(read_through_const_cast_struct_ptr() == 7);

int main() {
	return 0;
}
